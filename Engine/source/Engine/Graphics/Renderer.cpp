#include "Renderer.h"

namespace Okay
{
	struct GPURenderData
	{
		glm::mat4 viewProjMatrix = glm::mat4(1.f);
		glm::vec3 cameraPos = glm::vec3(0.f);
		float pad0;
		glm::vec3 cameraDir = glm::vec3(0.f);
		float pad1;
	};

	struct GPUObjectData
	{
		glm::mat4 objectMatrix = glm::mat4(1.f);
	};

	void Renderer::initialize(const Window& window)
	{
#ifndef NDEBUG
		enableDebugLayer();
		enableGPUBasedValidation();
#endif

		IDXGIFactory* pFactory = nullptr;
		DX_CHECK(CreateDXGIFactory(IID_PPV_ARGS(&pFactory)));

		createDevice(pFactory);
		m_commandContext.initialize(m_pDevice);

		createSwapChain(pFactory, window);

		m_gpuResourceManager.initialize(m_pDevice, m_commandContext);
		m_descriptorHeapStore.initialize(m_pDevice, 10);

		fetchBackBuffersAndDSV();

		m_cbvSrvUavDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_dsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		createMainRenderPass();

		ResourceHandle renderDataResource = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_UPLOAD, RESOURCE_PLACEMENT_ALIGNMENT);

		m_renderDataAH = m_gpuResourceManager.addConstantBuffer(renderDataResource, sizeof(GPURenderData), nullptr);
		m_instancedObjectDataAH = m_gpuResourceManager.addStructuredBuffer(renderDataResource, sizeof(GPUObjectData), 10, nullptr);

		struct Vertex
		{
			glm::vec3 position;
			glm::vec4 colour;
		};

		Vertex verticies[] =
		{
			{.position = glm::vec3(-0.5f, -0.5f, 0.f), .colour = glm::vec4(1.f, 0.f, 0.f, 1.f) },
			{.position = glm::vec3(0.5f, -0.5f, 0.0f), .colour = glm::vec4(0.f, 0.f, 1.f, 1.f) },
			{.position = glm::vec3(0.f, 0.5f, 0.f), .colour = glm::vec4(0.f, 1.f, 0.f, 1.f) },
		};

		uint32_t indicies[] =
		{
			0, 2, 1
		};


		ResourceHandle triangleBufferRH = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, 1'000);
		m_triangleColourAH = m_gpuResourceManager.addConstantBuffer(triangleBufferRH, 16, nullptr);
		m_vertexBufferAH = m_gpuResourceManager.addStructuredBuffer(triangleBufferRH, sizeof(verticies[0]), _countof(verticies), &verticies);

		ResourceHandle indexBufferRH = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, sizeof(indicies));
		m_indexBufferAH = m_gpuResourceManager.addStructuredBuffer(indexBufferRH, sizeof(indicies[0]), _countof(indicies), &indicies);
		m_commandContext.transitionResource(m_gpuResourceManager.getDXResource(indexBufferRH), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
	}

	void Renderer::shutdown()
	{
		m_commandContext.shutdown();
		m_gpuResourceManager.shutdown();

		m_descriptorHeapStore.shutdown();

		m_mainRenderPass.shutdown();

		for (uint32_t i = 0; i < NUM_BACKBUFFERS; i++)
			D3D12_RELEASE(m_backBuffers[i]);

		D3D12_RELEASE(m_pSwapChain);
		D3D12_RELEASE(m_pDevice);
	}

	void Renderer::render(const Scene& scene)
	{
		updateBuffers(scene);

		preRender();
		renderScene(scene);
		postRender();
	}

	void Renderer::updateBuffers(const Scene& scene)
	{
		const Entity camEntity = scene.getActiveCamera();
		const Transform& camTransform = camEntity.getComponent<Transform>();
		const Camera& cameraComp = camEntity.getComponent<Camera>();

		GPURenderData renderData{};
		renderData.cameraPos = camTransform.position;
		renderData.cameraDir = camTransform.forwardVec();
		renderData.viewProjMatrix = glm::transpose(cameraComp.getProjectionMatrix(m_viewport.Width, m_viewport.Height) * camTransform.getViewMatrix());

		m_gpuResourceManager.updateBuffer(m_renderDataAH, &renderData);

		glm::vec4 colour = glm::vec4(rand() / (float)RAND_MAX, rand() / (float)RAND_MAX, rand() / (float)RAND_MAX, 1.f);
		m_gpuResourceManager.updateBuffer(m_triangleColourAH, &colour);
	}

	void Renderer::preRender()
	{
		m_currentBackBuffer = (m_currentBackBuffer + 1) % NUM_BACKBUFFERS;
		m_commandContext.transitionResource(m_backBuffers[m_currentBackBuffer], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		D3D12_CPU_DESCRIPTOR_HANDLE dsvCPUDescriptorHandle = m_descriptorHeapStore.getCPUHandle(m_dsvDescriptor);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvCPUDescriptorHandle = m_descriptorHeapStore.getCPUHandle(m_rtvFirstDescriptor);
		rtvCPUDescriptorHandle.ptr += (uint64_t)m_currentBackBuffer * m_rtvDescriptorSize;

		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();

		float testClearColor[4] = { 0.9f, 0.5f, 0.4f, 1.f };
		pCommandList->ClearRenderTargetView(rtvCPUDescriptorHandle, testClearColor, 0, nullptr);
		pCommandList->ClearDepthStencilView(dsvCPUDescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

		pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		pCommandList->RSSetViewports(1, &m_viewport);
		pCommandList->RSSetScissorRects(1, &m_scissorRect);

		pCommandList->OMSetRenderTargets(1, &rtvCPUDescriptorHandle, false, &dsvCPUDescriptorHandle);
	}

	void Renderer::renderScene(const Scene& scene)
	{
		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();

		m_mainRenderPass.bind(pCommandList);

		pCommandList->SetGraphicsRootConstantBufferView(0, m_gpuResourceManager.getVirtualAddress(m_renderDataAH));
		pCommandList->SetGraphicsRootShaderResourceView(1, m_gpuResourceManager.getVirtualAddress(m_instancedObjectDataAH));
		pCommandList->SetGraphicsRootConstantBufferView(2, m_gpuResourceManager.getVirtualAddress(m_triangleColourAH));
		pCommandList->SetGraphicsRootShaderResourceView(3, m_gpuResourceManager.getVirtualAddress(m_vertexBufferAH));

		D3D12_INDEX_BUFFER_VIEW ibView = {};
		ibView.BufferLocation = m_gpuResourceManager.getVirtualAddress(m_indexBufferAH);
		ibView.SizeInBytes = m_gpuResourceManager.getTotalSize(m_indexBufferAH);
		ibView.Format = DXGI_FORMAT_R32_UINT;

		pCommandList->IASetIndexBuffer(&ibView);

		auto meshRendererView = scene.getRegistry().view<MeshRenderer, Transform>();
		uint32_t numMeshRenders = (uint32_t)meshRendererView.size_hint();

		// Temp
		OKAY_ASSERT(numMeshRenders <= m_gpuResourceManager.getAllocation(m_instancedObjectDataAH).numElements);

		static std::vector<GPUObjectData> objectDatas;
		objectDatas.reserve(numMeshRenders);
		objectDatas.clear();

		for (entt::entity entity : meshRendererView)
		{
			auto [meshRenderer, transform] = meshRendererView[entity];

			GPUObjectData& objectData = objectDatas.emplace_back();
			objectData.objectMatrix = glm::transpose(transform.getMatrix());
		}

		m_gpuResourceManager.updateBuffer(m_instancedObjectDataAH, objectDatas.data());
		pCommandList->DrawIndexedInstanced(3, numMeshRenders, 0, 0, 0);
	}

	void Renderer::postRender()
	{
		m_commandContext.transitionResource(m_backBuffers[m_currentBackBuffer], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		m_commandContext.execute();
		m_pSwapChain->Present(1, 0);

		m_commandContext.wait();
		m_commandContext.reset();
	}

	void Renderer::createDevice(IDXGIFactory* pFactory)
	{
		IDXGIAdapter* pAdapter = nullptr;
		uint32_t adapterIndex = 0;

		while (!pAdapter)
		{
			DX_CHECK(pFactory->EnumAdapters(adapterIndex, &pAdapter));

			HRESULT hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device*), nullptr);
			if (FAILED(hr))
			{
				D3D12_RELEASE(pAdapter);
			}

			adapterIndex++;
		}

		D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_pDevice));

		logAdapterInfo(pAdapter);

		D3D12_RELEASE(pAdapter);
	}

	void Renderer::createSwapChain(IDXGIFactory* pFactory, const Window& window)
	{
		IDXGIFactory2* pFactory2 = nullptr;
		DX_CHECK(pFactory->QueryInterface(IID_PPV_ARGS(&pFactory2)));

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Width = 0;
		swapChainDesc.Height = 0;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = NUM_BACKBUFFERS;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapChainDesc.Flags = 0;

		DX_CHECK(pFactory2->CreateSwapChainForHwnd(m_commandContext.getCommandQueue(), window.getHWND(), &swapChainDesc, nullptr, nullptr, &m_pSwapChain));

		D3D12_RELEASE(pFactory2);
	}

	void Renderer::fetchBackBuffersAndDSV()
	{
		DescriptorDesc backBufferRTVs[NUM_BACKBUFFERS] = {};

		for (uint32_t i = 0; i < NUM_BACKBUFFERS; i++)
		{
			DX_CHECK(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));

			backBufferRTVs[i].type = OKAY_DESCRIPTOR_TYPE_RTV;
			backBufferRTVs[i].pDXResource = m_backBuffers[i];
		}

		m_rtvFirstDescriptor = m_descriptorHeapStore.createDescriptors(NUM_BACKBUFFERS, backBufferRTVs, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Create depth stencil texture & descriptor
		D3D12_RESOURCE_DESC resourceDesc = m_backBuffers[0]->GetDesc();
		ResourceHandle dsHandle = m_gpuResourceManager.createTexture((uint32_t)resourceDesc.Width, resourceDesc.Height, DXGI_FORMAT_D32_FLOAT, OKAY_TEXTURE_FLAG_DEPTH, nullptr);

		DescriptorDesc dsvDesc = m_gpuResourceManager.createDescriptorDesc(dsHandle, OKAY_DESCRIPTOR_TYPE_DSV, true);
		m_dsvDescriptor = m_descriptorHeapStore.createDescriptors(1, &dsvDesc, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		ID3D12Resource* pDsvResource = m_gpuResourceManager.getDXResource(dsHandle);
		m_commandContext.transitionResource(pDsvResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		// Find viewport & scissor rect
		D3D12_RESOURCE_DESC backBufferDesc = m_backBuffers[0]->GetDesc();

		m_viewport.TopLeftX = 0.f;
		m_viewport.TopLeftY = 0.f;
		m_viewport.Width = (float)backBufferDesc.Width;
		m_viewport.Height = (float)backBufferDesc.Height;
		m_viewport.MinDepth = 0;
		m_viewport.MaxDepth = 1;

		m_scissorRect.left = 0;
		m_scissorRect.top = 0;
		m_scissorRect.right = (LONG)backBufferDesc.Width;
		m_scissorRect.bottom = (LONG)backBufferDesc.Height;
	}

	void Renderer::createMainRenderPass()
	{
		D3D12_ROOT_PARAMETER rootParams[4] = {};

		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[0].Descriptor.ShaderRegister = 0;
		rootParams[0].Descriptor.RegisterSpace = 0;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[1].Descriptor.ShaderRegister = 1;
		rootParams[1].Descriptor.RegisterSpace = 0;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[2].Descriptor.ShaderRegister = 2;
		rootParams[2].Descriptor.RegisterSpace = 0;
		rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[3].Descriptor.ShaderRegister = 3;
		rootParams[3].Descriptor.RegisterSpace = 0;
		rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = _countof(rootParams);
		rootSignatureDesc.pParameters = rootParams;

		rootSignatureDesc.NumStaticSamplers = 0;
		rootSignatureDesc.pStaticSamplers = nullptr;


		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = createDefaultGraphicsPipelineStateDesc();
		pipelineDesc.NumRenderTargets = 1;
		pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

		ID3DBlob* pVSBlob = nullptr;
		ID3DBlob* pPSBlob = nullptr;

		pipelineDesc.VS = compileShader(SHADER_PATH / "VertexShader.hlsl", "vs_5_1", &pVSBlob);
		pipelineDesc.PS = compileShader(SHADER_PATH / "PixelShader.hlsl", "ps_5_1", &pPSBlob);


		m_mainRenderPass.initialize(m_pDevice, pipelineDesc, rootSignatureDesc);

		D3D12_RELEASE(pVSBlob);
		D3D12_RELEASE(pPSBlob);
	}

	void Renderer::enableDebugLayer()
	{
		ID3D12Debug* pDebugController = nullptr;
		DX_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));

		pDebugController->EnableDebugLayer();
		D3D12_RELEASE(pDebugController);
	}

	void Renderer::enableGPUBasedValidation()
	{
		ID3D12Debug1* pDebugController = nullptr;
		DX_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));

		pDebugController->SetEnableGPUBasedValidation(true);
		D3D12_RELEASE(pDebugController);
	}
}

#include "Renderer.h"
#include "Engine/Resources/ResourceManager.h"

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

		m_ringBuffer.initialize(m_pDevice, 10'000'000);

		m_gpuResourceManager.initialize(m_pDevice, m_commandContext, m_ringBuffer);
		m_descriptorHeapStore.initialize(m_pDevice, 50);

		fetchBackBuffersAndDSV();

		m_cbvSrvUavDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_dsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		createMainRenderPass();

		m_activeDrawGroups = 0;
		m_drawGroups.resize(50);
		for (DrawGroup& drawGroup : m_drawGroups)
		{
			drawGroup.entities.reserve(50);
		}
	}

	void Renderer::shutdown()
	{
		m_commandContext.shutdown();
		m_gpuResourceManager.shutdown();

		m_ringBuffer.shutdown();

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

	void Renderer::preProcessResources(const ResourceManager& resourceManager)
	{
		const std::vector<Mesh>& meshes = resourceManager.getAll<Mesh>();

		uint64_t verticiesResourceSize = 0;
		uint64_t indiciesResourceSize = 0;

		for (const Mesh& mesh : meshes)
		{
			const MeshData& meshData = mesh.getMeshData();
			verticiesResourceSize += alignAddress64(meshData.verticies.size() * sizeof(Vertex), BUFFER_DATA_ALIGNMENT);
			indiciesResourceSize += alignAddress64(meshData.indicies.size() * sizeof(uint32_t), BUFFER_DATA_ALIGNMENT);
		}

		ResourceHandle verticiesRH = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, verticiesResourceSize);
		ResourceHandle indiciesRH = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, indiciesResourceSize);

		m_dxMeshes.resize(meshes.size());

		for (uint32_t i = 0; i < (uint32_t)meshes.size(); i++)
		{
			const std::vector<Vertex>& verticies = meshes[i].getMeshData().verticies;
			const std::vector<uint32_t>& indicies = meshes[i].getMeshData().indicies;

			Allocation verticiesAlloc = m_gpuResourceManager.allocateInto(verticiesRH, OKAY_RESOURCE_APPEND, sizeof(Vertex), (uint32_t)verticies.size(), verticies.data());
			Allocation indiciesAlloc = m_gpuResourceManager.allocateInto(indiciesRH, OKAY_RESOURCE_APPEND, sizeof(uint32_t), (uint32_t)indicies.size(), indicies.data());

			m_dxMeshes[i].gpuVerticies = m_gpuResourceManager.getVirtualAddress(verticiesAlloc);

			m_dxMeshes[i].indiciesView.BufferLocation = m_gpuResourceManager.getVirtualAddress(indiciesAlloc);
			m_dxMeshes[i].indiciesView.SizeInBytes = (uint32_t)indiciesAlloc.elementSize * indiciesAlloc.numElements;
			m_dxMeshes[i].indiciesView.Format = DXGI_FORMAT_R32_UINT;

			m_dxMeshes[i].numIndicies = (uint32_t)indicies.size();
		}

		m_commandContext.transitionResource(m_gpuResourceManager.getDXResource(indiciesRH), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
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

		m_renderData = m_ringBuffer.allocate(&renderData, sizeof(GPURenderData));
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
		pCommandList->SetGraphicsRootConstantBufferView(0, m_renderData);

		// Render Objects

		assignObjectDrawGroups(scene);

		auto meshRendererView = scene.getRegistry().view<MeshRenderer, Transform>();


		uint8_t* pMappedPtr = m_ringBuffer.map();
		D3D12_GPU_VIRTUAL_ADDRESS drawGroupVirtaulAddress = m_ringBuffer.getCurrentGPUAddress();
		uint64_t ringBufferBytesWritten = 0;

		for (uint32_t i = 0; i < m_activeDrawGroups; i++)
		{
			DrawGroup& drawGroup = m_drawGroups[i];

			for (entt::entity entity : drawGroup.entities)
			{
				const Transform& transform = meshRendererView.get<Transform>(entity);

				GPUObjectData* pObjectData = (GPUObjectData*)pMappedPtr;
				pObjectData->objectMatrix = glm::transpose(transform.getMatrix());

				pMappedPtr += sizeof(GPUObjectData);
			}

			const DXMesh& dxMesh = m_dxMeshes[drawGroup.dxMeshId];

			pCommandList->SetGraphicsRootShaderResourceView(2, drawGroupVirtaulAddress);

			pCommandList->SetGraphicsRootShaderResourceView(1, dxMesh.gpuVerticies);
			pCommandList->IASetIndexBuffer(&dxMesh.indiciesView);

			pCommandList->DrawIndexedInstanced(dxMesh.numIndicies, (uint32_t)drawGroup.entities.size(), 0, 0, 0);

			uint64_t bytesWritten = drawGroup.entities.size() * sizeof(GPUObjectData);
			drawGroupVirtaulAddress += bytesWritten;
			ringBufferBytesWritten += bytesWritten;

			drawGroup.entities.clear();
		}

		m_ringBuffer.unmap(ringBufferBytesWritten);
	}

	void Renderer::postRender()
	{
		m_commandContext.transitionResource(m_backBuffers[m_currentBackBuffer], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		m_commandContext.execute();
		m_pSwapChain->Present(1, 0);

		m_commandContext.wait();
		m_commandContext.reset();

		m_ringBuffer.jumpToStart();
	}

	void Renderer::assignObjectDrawGroups(const Scene& scene)
	{
		auto meshRendererView = scene.getRegistry().view<MeshRenderer, Transform>();

		m_activeDrawGroups = 0;
		for (entt::entity entity : meshRendererView)
		{
			const MeshRenderer& meshRenderer = meshRendererView.get<MeshRenderer>(entity);

			uint32_t drawGroupIdx = INVALID_UINT32;
			for (uint32_t i = 0; i < m_activeDrawGroups; i++)
			{
				if (m_drawGroups[i].dxMeshId == meshRenderer.meshID)
				{
					drawGroupIdx = i;
					break;
				}
			}

			if (drawGroupIdx == INVALID_UINT32)
			{
				if (m_activeDrawGroups >= (uint32_t)m_drawGroups.size())
				{
					m_drawGroups.resize(m_drawGroups.size() + 20);
				}

				drawGroupIdx = m_activeDrawGroups++;
				m_drawGroups[drawGroupIdx].dxMeshId = meshRenderer.meshID;
			}

			m_drawGroups[drawGroupIdx].entities.emplace_back(entity);
		}
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

		m_rtvFirstDescriptor = m_descriptorHeapStore.allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, backBufferRTVs, NUM_BACKBUFFERS);

		// Create depth stencil texture & descriptor
		D3D12_RESOURCE_DESC resourceDesc = m_backBuffers[0]->GetDesc();
		Allocation dsAllocation = m_gpuResourceManager.createTexture((uint32_t)resourceDesc.Width, resourceDesc.Height, DXGI_FORMAT_D32_FLOAT, OKAY_TEXTURE_FLAG_DEPTH, nullptr);

		DescriptorDesc dsvDesc = m_gpuResourceManager.createDescriptorDesc(dsAllocation, OKAY_DESCRIPTOR_TYPE_DSV, true);
		m_dsvDescriptor = m_descriptorHeapStore.allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &dsvDesc, 1);

		ID3D12Resource* pDsvResource = m_gpuResourceManager.getDXResource(dsAllocation.resourceHandle);
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
		D3D12_ROOT_PARAMETER rootParams[3] = {};

		// Main Render Data (GPURenderData)
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[0].Descriptor.ShaderRegister = 0;
		rootParams[0].Descriptor.RegisterSpace = 0;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// Verticies SRV
		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[1].Descriptor.ShaderRegister = 0;
		rootParams[1].Descriptor.RegisterSpace = 0;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// Object datas (GPUObjcetData)
		rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[2].Descriptor.ShaderRegister = 1;
		rootParams[2].Descriptor.RegisterSpace = 0;
		rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;


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

#include "Renderer.h"
#include "Engine/Resources/ResourceManager.h"

namespace Okay
{
	struct GPURenderData
	{
		glm::mat4 viewProjMatrix = glm::mat4(1.f);
		glm::vec3 cameraPos = glm::vec3(0.f);
		uint32_t numPointLights = 0;
		glm::vec3 cameraDir = glm::vec3(0.f);
		uint32_t numDirectionalLights = 0;
		uint32_t numSpotLights = 0;
	};

	struct GPUObjectData
	{
		glm::mat4 objectMatrix = glm::mat4(1.f);
		uint32_t textureIdx = 0;
	};

	struct GPUPointLight
	{
		glm::vec3 position = glm::vec3(0.f);
		PointLight lightData;
	};

	struct GPUDirectionalLight
	{
		glm::vec3 direction = glm::vec3(0.f);
		DirectionalLight lightData;
	};

	struct GPUSpotLight
	{
		glm::vec3 position = glm::vec3(0.f);
		glm::vec3 direction = glm::vec3(0.f);
		SpotLight lightData; // spreadAngle is used as the cosine of the spreadAngle in the GPU version
	};
	
	template<typename EntityView, typename GPUComponent, typename WriteFunc>
	static uint64_t writeLightData(EntityView view, GPUComponent* pComponent, WriteFunc writeFunc)
	{
		for (entt::entity entity : view)
		{
			auto [lightComp, transform] = view[entity];

			pComponent->lightData = lightComp;
			writeFunc(pComponent, transform);

			pComponent += 1;
		}

		return alignAddress64(view.size_hint() * sizeof(GPUComponent), BUFFER_DATA_ALIGNMENT);
	}

	void Renderer::initialize(const Window& window)
	{
#ifndef NDEBUG
		enableDebugLayer();
		enableGPUBasedValidation();
#endif

		IDXGIFactory* pFactory = nullptr;
		DX_CHECK(CreateDXGIFactory(IID_PPV_ARGS(&pFactory)));

		createDevice(pFactory);
		m_commandContext.initialize(m_pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT);

		createSwapChain(pFactory, window);

		m_rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		m_ringBuffer.initialize(m_pDevice, 50'000'000);
		m_descriptorHeapStore.initialize(m_pDevice, 50);
		m_gpuResourceManager.initialize(m_pDevice, m_commandContext, m_ringBuffer, m_descriptorHeapStore);

		fetchBackBuffersAndDSV();

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
		preProcessMeshes(resourceManager.getAll<Mesh>());
		preProcessTextures(resourceManager.getAll<Texture>());
	}

	void Renderer::updateBuffers(const Scene& scene)
	{
		const Entity camEntity = scene.getActiveCamera();
		const Transform& camTransform = camEntity.getComponent<Transform>();
		const Camera& cameraComp = camEntity.getComponent<Camera>();


		uint8_t* pMappedRingBuffer = m_ringBuffer.map();
		uint64_t ringBufferBytesWritten = 0;
		uint64_t lightBytesWritten = 0;

		m_pointLightsGVA = m_ringBuffer.getCurrentGPUAddress();
		auto pointLightView = scene.getRegistry().view<PointLight, Transform>();
		lightBytesWritten = writeLightData(pointLightView, (GPUPointLight*)pMappedRingBuffer, [](GPUPointLight* pGPUPointLight, const Transform& transform)
			{
				pGPUPointLight->position = transform.position;
			});
		ringBufferBytesWritten += lightBytesWritten;
		pMappedRingBuffer += lightBytesWritten;


		m_directionalLightsGVA = m_ringBuffer.getCurrentGPUAddress() + ringBufferBytesWritten;
		auto directionalLightView = scene.getRegistry().view<DirectionalLight, Transform>();
		lightBytesWritten = writeLightData(directionalLightView, (GPUDirectionalLight*)pMappedRingBuffer, [](GPUDirectionalLight* pGPUDirLight, const Transform& transform)
			{
				pGPUDirLight->direction = -transform.forwardVec();
			});
		ringBufferBytesWritten += lightBytesWritten;
		pMappedRingBuffer += lightBytesWritten;


		m_spotLightsGVA = m_ringBuffer.getCurrentGPUAddress() + ringBufferBytesWritten;
		auto spotLightView = scene.getRegistry().view<SpotLight, Transform>();
		lightBytesWritten = writeLightData(spotLightView, (GPUSpotLight*)pMappedRingBuffer, [](GPUSpotLight* pGPUSpotLight, const Transform& transform)
			{
				pGPUSpotLight->position = transform.position;
				pGPUSpotLight->direction = transform.forwardVec();
				pGPUSpotLight->lightData.spreadAngle = glm::cos(glm::radians(pGPUSpotLight->lightData.spreadAngle * 0.5f));
			});
		ringBufferBytesWritten += lightBytesWritten;
		pMappedRingBuffer += lightBytesWritten;


		GPURenderData renderData{};
		renderData.cameraPos = camTransform.position;
		renderData.cameraDir = camTransform.forwardVec();
		renderData.viewProjMatrix = glm::transpose(cameraComp.getProjectionMatrix(m_viewport.Width, m_viewport.Height) * camTransform.getViewMatrix());

		renderData.numPointLights = (uint32_t)pointLightView.size_hint();
		renderData.numDirectionalLights = (uint32_t)directionalLightView.size_hint();
		renderData.numSpotLights = (uint32_t)spotLightView.size_hint();

		m_renderDataGVA = m_ringBuffer.getCurrentGPUAddress() + ringBufferBytesWritten;
		memcpy(pMappedRingBuffer, &renderData, sizeof(GPURenderData));

		ringBufferBytesWritten += alignAddress64(sizeof(GPURenderData), BUFFER_DATA_ALIGNMENT);
		pMappedRingBuffer = (uint8_t*)alignAddress64((uint64_t)pMappedRingBuffer, BUFFER_DATA_ALIGNMENT);


		m_ringBuffer.unmap(ringBufferBytesWritten);
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

		ID3D12DescriptorHeap* pMaterialDXDescHeap = m_descriptorHeapStore.getDXDescriptorHeap(m_materialTexturesDHH);
		pCommandList->SetDescriptorHeaps(1, &pMaterialDXDescHeap);

		pCommandList->SetGraphicsRootConstantBufferView(0, m_renderDataGVA);
		pCommandList->SetGraphicsRootDescriptorTable(3, pMaterialDXDescHeap->GetGPUDescriptorHandleForHeapStart());
		pCommandList->SetGraphicsRootShaderResourceView(4, m_pointLightsGVA);
		pCommandList->SetGraphicsRootShaderResourceView(5, m_directionalLightsGVA);
		pCommandList->SetGraphicsRootShaderResourceView(6, m_spotLightsGVA);

		// Render Objects

		assignObjectDrawGroups(scene);

		uint8_t* pMappedObjectDatas = m_ringBuffer.map();
		D3D12_GPU_VIRTUAL_ADDRESS drawGroupObjectDatasVA = m_ringBuffer.getCurrentGPUAddress();
		uint64_t ringBufferBytesWritten = 0;

		auto meshRendererView = scene.getRegistry().view<MeshRenderer, Transform>();

		for (uint32_t i = 0; i < m_activeDrawGroups; i++)
		{
			DrawGroup& drawGroup = m_drawGroups[i];

			for (entt::entity entity : drawGroup.entities)
			{
				auto [meshRenderer, transform] = meshRendererView[entity];

				GPUObjectData* pObjectData = (GPUObjectData*)pMappedObjectDatas;
				pObjectData->objectMatrix = glm::transpose(transform.getMatrix());
				pObjectData->textureIdx = meshRenderer.textureID;

				pMappedObjectDatas += sizeof(GPUObjectData);
			}

			const DXMesh& dxMesh = m_dxMeshes[drawGroup.dxMeshId];

			pCommandList->SetGraphicsRootShaderResourceView(2, drawGroupObjectDatasVA);

			pCommandList->SetGraphicsRootShaderResourceView(1, dxMesh.gpuVerticiesGVA);
			pCommandList->IASetIndexBuffer(&dxMesh.indiciesView);

			pCommandList->DrawIndexedInstanced(dxMesh.numIndicies, (uint32_t)drawGroup.entities.size(), 0, 0, 0);

			uint64_t bytesWritten = drawGroup.entities.size() * sizeof(GPUObjectData);
			drawGroupObjectDatasVA += bytesWritten;
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
		Allocation dsAllocation = m_gpuResourceManager.createTexture((uint32_t)resourceDesc.Width, resourceDesc.Height, 1, DXGI_FORMAT_D32_FLOAT, OKAY_TEXTURE_FLAG_DEPTH, nullptr);

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
		D3D12_ROOT_PARAMETER rootParams[7] = {};

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
		
		// Textures
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 256; // At this point we don't know the real number of textures, so just setting a high upper limit
		range.BaseShaderRegister = 2;
		range.RegisterSpace = 1;
		range.OffsetInDescriptorsFromTableStart = 0;

		rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[3].DescriptorTable.pDescriptorRanges = &range;
		rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		// Point lights
		rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[4].Descriptor.ShaderRegister = 3;
		rootParams[4].Descriptor.RegisterSpace = 0;
		rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		// Directional lights
		rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[5].Descriptor.ShaderRegister = 4;
		rootParams[5].Descriptor.RegisterSpace = 0;
		rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		// Spot lights
		rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[6].Descriptor.ShaderRegister = 5;
		rootParams[6].Descriptor.RegisterSpace = 0;
		rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


		D3D12_STATIC_SAMPLER_DESC samplerDesc = createDefaultStaticPointSamplerDesc();
		samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
		samplerDesc.MaxAnisotropy = 4;


		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = _countof(rootParams);
		rootSignatureDesc.pParameters = rootParams;

		rootSignatureDesc.NumStaticSamplers = 1;
		rootSignatureDesc.pStaticSamplers = &samplerDesc;


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

	void Renderer::preProcessMeshes(const std::vector<Mesh>& meshes)
	{
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

			m_dxMeshes[i].gpuVerticiesGVA = m_gpuResourceManager.getVirtualAddress(verticiesAlloc);

			m_dxMeshes[i].indiciesView.BufferLocation = m_gpuResourceManager.getVirtualAddress(indiciesAlloc);
			m_dxMeshes[i].indiciesView.SizeInBytes = (uint32_t)indiciesAlloc.elementSize * indiciesAlloc.numElements;
			m_dxMeshes[i].indiciesView.Format = DXGI_FORMAT_R32_UINT;

			m_dxMeshes[i].numIndicies = (uint32_t)indicies.size();
		}

		m_commandContext.transitionResource(m_gpuResourceManager.getDXResource(indiciesRH), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
	}

	void Renderer::preProcessTextures(const std::vector<Texture>& textures)
	{
		m_materialTexturesDHH = m_descriptorHeapStore.createDescriptorHeap((uint32_t)textures.size(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		for (const Texture& texture : textures)
		{
			Allocation textureAlloc = m_gpuResourceManager.createTexture(texture.getWidth(), texture.getHeight(), MAX_MIP_LEVELS,
				DXGI_FORMAT_R8G8B8A8_UNORM, OKAY_TEXTURE_FLAG_SHADER_READ, texture.getTextureData());

			DescriptorDesc desc = m_gpuResourceManager.createDescriptorDesc(textureAlloc, OKAY_DESCRIPTOR_TYPE_SRV, true);
			m_descriptorHeapStore.allocateDescriptors(m_materialTexturesDHH, OKAY_DESCRIPTOR_APPEND, &desc, 1);
		}

		m_gpuResourceManager.generateMipMaps();
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

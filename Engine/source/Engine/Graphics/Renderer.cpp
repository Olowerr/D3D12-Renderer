#include "Renderer.h"
#include "Engine/Resources/ResourceManager.h"

#include "Engine/Application/ImguiHelper.h"

#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_dx12.h"

namespace Okay
{
	static D3D12_RESOURCE_BARRIER s_shadowMapBarriers[Renderer::MAX_SHADOW_MAPS] = {};

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
		uint32_t diffuseTextureIdx = 0;
		uint32_t normalMapIdx = 0;
	};

	struct GPUPointLight
	{
		glm::vec3 position = glm::vec3(0.f);

		glm::vec3 colour = glm::vec3(1.f);
		float intensity = 1.f;
		glm::vec2 attenuation = glm::vec2(0.f, 1.f);
	};

	struct GPUDirectionalLight
	{
		glm::mat4 viewProjMatrix = glm::mat4(1.f);
		uint32_t shadowMapIdx = INVALID_UINT32;

		glm::vec3 direction = glm::vec3(0.f);

		glm::vec3 colour = glm::vec3(1.f);
		float intensity = 1.f;
	};

	struct GPUSpotLight
	{
		glm::mat4 viewProjMatrix = glm::mat4(1.f);
		uint32_t shadowMapIdx = INVALID_UINT32;

		glm::vec3 position = glm::vec3(0.f);
		glm::vec3 direction = glm::vec3(0.f);

		glm::vec3 colour = glm::vec3(1.f);
		float intensity = 1.f;

		glm::vec2 attenuation = glm::vec2(0.f, 1.f);
		float spreadCosAngle = 90.f;
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
		m_commandContext.initialize(m_pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT);

		createSwapChain(pFactory, window);

		m_rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		m_ringBuffer.initialize(m_pDevice, 50'000'000);
		m_descriptorHeapStore.initialize(m_pDevice, 50);
		m_gpuResourceManager.initialize(m_pDevice, m_commandContext, m_ringBuffer, m_descriptorHeapStore);

		fetchBackBuffersAndDSV();
		createRenderPasses(); // need to be after fetching backBuffers cuz it needs the main viewport

		m_activeDrawGroups = 0;
		m_drawGroups.resize(50);
		for (DrawGroup& drawGroup : m_drawGroups)
		{
			drawGroup.entities.reserve(50);
		}


		m_materialTexturesDHH = m_descriptorHeapStore.createDescriptorHeap(256, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		m_shadowMapPool.reserve(MAX_SHADOW_MAPS);


		// In this version of Imgui, only 1 SRV is needed, it's stated that future versions will need more, but I don't see a reason to switch version atm :]
		DescriptorHeapHandle imguiHeapHandle = m_descriptorHeapStore.createDescriptorHeap(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_pImguiDescriptorHeap = m_descriptorHeapStore.getDXDescriptorHeap(imguiHeapHandle);

		imguiInitialize(window, m_pDevice, m_commandContext.getCommandQueue(), m_pImguiDescriptorHeap);
	}

	void Renderer::shutdown()
	{
		imguiShutdown();

		m_commandContext.shutdown();
		m_gpuResourceManager.shutdown();

		m_ringBuffer.shutdown();

		m_descriptorHeapStore.shutdown();

		m_mainRenderPass.shutdown();
		m_shadowPass.shutdown();

		for (uint32_t i = 0; i < NUM_BACKBUFFERS; i++)
			D3D12_RELEASE(m_backBuffers[i]);

		D3D12_RELEASE(m_pSwapChain);
		D3D12_RELEASE(m_pDevice);
	}

	void Renderer::render(const Scene& scene)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE currentMainRtv = {};
		uint32_t numShadowMaps = 0;

		updateBuffers(scene);

		preRender(&currentMainRtv);
		renderScene(scene, currentMainRtv);
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

		auto pointLightView = scene.getRegistry().view<PointLight, Transform>();
		auto directionalLightView = scene.getRegistry().view<DirectionalLight, Transform>();
		auto spotLightView = scene.getRegistry().view<SpotLight, Transform>();

		uint8_t* pMappedRingBuffer = m_ringBuffer.map();
		uint64_t ringBufferOffset = 0;

		m_numActiveShadowMaps = 0;


		m_pointLightsGVA = m_ringBuffer.getCurrentGPUAddress() + ringBufferOffset;
		ringBufferOffset += writePointLightData(pointLightView, pMappedRingBuffer + ringBufferOffset);

		m_directionalLightsGVA = m_ringBuffer.getCurrentGPUAddress() + ringBufferOffset;
		ringBufferOffset += writeDirLightData(directionalLightView, pMappedRingBuffer + ringBufferOffset, camTransform);

		m_spotLightsGVA = m_ringBuffer.getCurrentGPUAddress() + ringBufferOffset;
		ringBufferOffset += writeSpotLightData(spotLightView, pMappedRingBuffer + ringBufferOffset);


		GPURenderData renderData{};
		renderData.cameraPos = camTransform.position;
		renderData.cameraDir = camTransform.forwardVec();
		renderData.viewProjMatrix = glm::transpose(cameraComp.getProjectionMatrix(m_viewport.Width, m_viewport.Height) * camTransform.getViewMatrix());

		renderData.numPointLights = (uint32_t)pointLightView.size_hint();
		renderData.numDirectionalLights = (uint32_t)directionalLightView.size_hint();
		renderData.numSpotLights = (uint32_t)spotLightView.size_hint();

		m_renderDataGVA = m_ringBuffer.getCurrentGPUAddress() + ringBufferOffset;
		memcpy(pMappedRingBuffer + ringBufferOffset, &renderData, sizeof(GPURenderData));
		ringBufferOffset += alignAddress64(sizeof(GPURenderData), BUFFER_DATA_ALIGNMENT);

		m_ringBuffer.unmap(ringBufferOffset);
	}

	void Renderer::preRender(D3D12_CPU_DESCRIPTOR_HANDLE* pOutCurrentBB)
	{
		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();

		m_currentBackBuffer = (m_currentBackBuffer + 1) % NUM_BACKBUFFERS;
		m_commandContext.transitionResource(m_backBuffers[m_currentBackBuffer], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		D3D12_CPU_DESCRIPTOR_HANDLE currentBackBufferRTV = m_rtvBackBufferCPUHandle;
		currentBackBufferRTV.ptr += (uint64_t)m_currentBackBuffer * m_rtvDescriptorSize;
		*pOutCurrentBB = currentBackBufferRTV;

		float testClearColor[4] = { 0.9f, 0.5f, 0.4f, 1.f };
		pCommandList->ClearRenderTargetView(currentBackBufferRTV, testClearColor, 0, nullptr);
		pCommandList->ClearDepthStencilView(m_mainDsvCpuHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);


		for (uint32_t i = 0; i < m_numActiveShadowMaps; i++)
		{
			s_shadowMapBarriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			s_shadowMapBarriers[i].Transition.pResource = m_gpuResourceManager.getDXResource(m_shadowMapPool[i].textureAllocation.resourceHandle);
			s_shadowMapBarriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			s_shadowMapBarriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			s_shadowMapBarriers[i].Transition.Subresource = 0;
			s_shadowMapBarriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		}
		pCommandList->ResourceBarrier(m_numActiveShadowMaps, s_shadowMapBarriers);

		for (uint32_t i = 0; i < m_numActiveShadowMaps; i++)
		{
			pCommandList->ClearDepthStencilView(m_shadowMapPool[i].dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
		}
	}

	void Renderer::renderScene(const Scene& scene, D3D12_CPU_DESCRIPTOR_HANDLE currentMainRtv)
	{
		assignObjectDrawGroups(scene);

		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();
		uint8_t* pMappedRingBuffer = m_ringBuffer.map();
		uint64_t ringBufferWrittenBytes = 0;

		m_shadowPass.bindBase(pCommandList);

		for (uint32_t i = 0; i < m_numActiveShadowMaps; i++)
		{
			const ShadowMap& shadowMap = m_shadowMapPool[i];

			GPURenderData* pRenderData = (GPURenderData*)pMappedRingBuffer;
			pRenderData->viewProjMatrix = shadowMap.viewProjMatrix;

			D3D12_GPU_VIRTUAL_ADDRESS lightCamBuffer = m_ringBuffer.getCurrentGPUAddress() + ringBufferWrittenBytes;
			pCommandList->SetGraphicsRootConstantBufferView(0, lightCamBuffer);

			m_shadowPass.bindRTVs(pCommandList, 0, nullptr, &shadowMap.dsvHandle);

			pMappedRingBuffer += alignAddress64(sizeof(GPURenderData), BUFFER_DATA_ALIGNMENT);
			ringBufferWrittenBytes += alignAddress64(sizeof(GPURenderData), BUFFER_DATA_ALIGNMENT);

			for (uint32_t k = 0; k < m_activeDrawGroups; k++)
			{
				const DrawGroup& drawGroup = m_drawGroups[k];

				const DXMesh& dxMesh = m_dxMeshes[drawGroup.dxMeshId];

				pCommandList->SetGraphicsRootShaderResourceView(1, dxMesh.gpuVerticiesGVA);
				pCommandList->IASetIndexBuffer(&dxMesh.indiciesView);

				pCommandList->SetGraphicsRootShaderResourceView(2, drawGroup.objectDatasVA);

				pCommandList->DrawIndexedInstanced(dxMesh.numIndicies, (uint32_t)drawGroup.entities.size(), 0, 0, 0);
			}

			s_shadowMapBarriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			s_shadowMapBarriers[i].Transition.pResource = m_gpuResourceManager.getDXResource(m_shadowMapPool[i].textureAllocation.resourceHandle);
			s_shadowMapBarriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			s_shadowMapBarriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			s_shadowMapBarriers[i].Transition.Subresource = 0;
			s_shadowMapBarriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

		}

		pCommandList->ResourceBarrier(m_numActiveShadowMaps, s_shadowMapBarriers);

		m_ringBuffer.unmap(ringBufferWrittenBytes);

		m_mainRenderPass.bind(pCommandList, 1, &currentMainRtv, &m_mainDsvCpuHandle);
		
		ID3D12DescriptorHeap* pMaterialDXDescHeap = m_descriptorHeapStore.getDXDescriptorHeap(m_materialTexturesDHH);
		pCommandList->SetDescriptorHeaps(1, &pMaterialDXDescHeap);

		pCommandList->SetGraphicsRootConstantBufferView(0, m_renderDataGVA);
		pCommandList->SetGraphicsRootDescriptorTable(3, pMaterialDXDescHeap->GetGPUDescriptorHandleForHeapStart());
		pCommandList->SetGraphicsRootShaderResourceView(4, m_pointLightsGVA);
		pCommandList->SetGraphicsRootShaderResourceView(5, m_directionalLightsGVA);
		pCommandList->SetGraphicsRootShaderResourceView(6, m_spotLightsGVA);


		// Render Objects
	
		for (uint32_t i = 0; i < m_activeDrawGroups; i++)
		{
			DrawGroup& drawGroup = m_drawGroups[i];

			const DXMesh& dxMesh = m_dxMeshes[drawGroup.dxMeshId];

			pCommandList->SetGraphicsRootShaderResourceView(1, dxMesh.gpuVerticiesGVA);
			pCommandList->IASetIndexBuffer(&dxMesh.indiciesView);

			pCommandList->SetGraphicsRootShaderResourceView(2, drawGroup.objectDatasVA);

			pCommandList->DrawIndexedInstanced(dxMesh.numIndicies, (uint32_t)drawGroup.entities.size(), 0, 0, 0);
		}
	}

	void Renderer::postRender()
	{
		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();

		ImGui::Render();
		pCommandList->SetDescriptorHeaps(1, &m_pImguiDescriptorHeap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCommandList);

		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

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

		for (DrawGroup& drawGroup : m_drawGroups)
		{
			drawGroup.entities.clear();
		}

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


		// Upload ObjectDatas

		uint8_t* pMappedRingBuffer = m_ringBuffer.map();
		uint64_t writtenBytes = 0;

		D3D12_GPU_VIRTUAL_ADDRESS drawGroupObjectDatasVA = m_ringBuffer.getCurrentGPUAddress();

		for (uint32_t i = 0; i < m_activeDrawGroups; i++)
		{
			DrawGroup& drawGroup = m_drawGroups[i];

			for (entt::entity entity : drawGroup.entities)
			{
				auto [meshRenderer, transform] = meshRendererView[entity];

				GPUObjectData* pObjectData = (GPUObjectData*)pMappedRingBuffer;
				pObjectData->objectMatrix = glm::transpose(transform.getMatrix());
				pObjectData->diffuseTextureIdx = meshRenderer.diffuseTextureID;
				pObjectData->normalMapIdx = meshRenderer.normalMapID;

				pMappedRingBuffer += sizeof(GPUObjectData);
				writtenBytes += sizeof(GPUObjectData);
			}

			drawGroup.objectDatasVA = drawGroupObjectDatasVA;
			drawGroupObjectDatasVA += drawGroup.entities.size() * sizeof(GPUObjectData);
		}

		m_ringBuffer.unmap(writtenBytes);
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

		m_rtvBackBufferCPUHandle = m_descriptorHeapStore.allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, backBufferRTVs, NUM_BACKBUFFERS).cpuHandle;

		// Create depth stencil texture & descriptor
		D3D12_RESOURCE_DESC resourceDesc = m_backBuffers[0]->GetDesc();
		Allocation dsAllocation = m_gpuResourceManager.createTexture((uint32_t)resourceDesc.Width, resourceDesc.Height, 1, DXGI_FORMAT_D32_FLOAT, OKAY_TEXTURE_FLAG_DEPTH, nullptr);

		DescriptorDesc dsvDesc = m_gpuResourceManager.createDescriptorDesc(dsAllocation, OKAY_DESCRIPTOR_TYPE_DSV, true);
		m_mainDsvCpuHandle = m_descriptorHeapStore.allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &dsvDesc, 1).cpuHandle;

		ID3D12Resource* pDsvResource = m_gpuResourceManager.getDXResource(dsAllocation.resourceHandle);
		m_commandContext.transitionResource(pDsvResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		// Find viewport & scissor rect
		D3D12_RESOURCE_DESC backBufferDesc = m_backBuffers[0]->GetDesc();

		m_viewport = createViewport((float)backBufferDesc.Width, (float)backBufferDesc.Height);
		m_scissorRect = createRect((uint32_t)backBufferDesc.Width, backBufferDesc.Height);
	}

	void Renderer::createRenderPasses()
	{
		D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
		samplers[0] = createDefaultStaticPointSamplerDesc();
		samplers[0].ShaderRegister = 0;

		samplers[1] = samplers[0];
		samplers[1].Filter = D3D12_FILTER_ANISOTROPIC;
		samplers[1].MaxAnisotropy = 4;
		samplers[1].ShaderRegister = 1;

		std::vector<D3D12_ROOT_PARAMETER> rootParams = {};
		rootParams.reserve(32);

		ID3DBlob* shaderBlobs[5] = {};
		uint32_t nextBlobIdx = 0;


		// Main Render Pass

		rootParams.emplace_back(createRootParamCBV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Main Render Data (GPURenderData)
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Verticies SRV
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 1, 0)); // Object datas (GPUObjcetData)
		
		// At this point we don't know the real number of textures, so just setting a high upper limit
		D3D12_DESCRIPTOR_RANGE textureDescriptorRanges[2] = { createRangeSRV(2, 1, 256, MAX_SHADOW_MAPS), createRangeSRV(6, 2, MAX_SHADOW_MAPS, 0) };
		rootParams.emplace_back(createRootParamTable(D3D12_SHADER_VISIBILITY_PIXEL, textureDescriptorRanges, 2)); // Textures + Shadow maps

		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_PIXEL, 3, 0)); // Point lights
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_PIXEL, 4, 0)); // Directional lights
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_PIXEL, 5, 0)); // Spot lights


		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = (uint32_t)rootParams.size();
		rootSignatureDesc.pParameters = rootParams.data();
		rootSignatureDesc.NumStaticSamplers = _countof(samplers);
		rootSignatureDesc.pStaticSamplers = samplers;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = createDefaultGraphicsPipelineStateDesc();
		pipelineDesc.NumRenderTargets = 1;
		pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

		pipelineDesc.VS = compileShader(SHADER_PATH / "VertexShader.hlsl", "vs_5_1", &shaderBlobs[nextBlobIdx++]);
		pipelineDesc.PS = compileShader(SHADER_PATH / "PixelShader.hlsl", "ps_5_1", &shaderBlobs[nextBlobIdx++]);

		m_mainRenderPass.initialize(m_pDevice, pipelineDesc, rootSignatureDesc);
		m_mainRenderPass.updateProperties(m_viewport, m_scissorRect, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		nextBlobIdx = 0;
		for (ID3DBlob*& pBlob : shaderBlobs)
			D3D12_RELEASE(pBlob);


		// Shadow Pass

		rootParams.clear();
		rootParams.emplace_back(createRootParamCBV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Light Data
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Verticies SRV
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 1, 0)); // Object datas (GPUObjcetData)

		rootSignatureDesc.NumParameters = (uint32_t)rootParams.size();
		rootSignatureDesc.pParameters = rootParams.data();

		pipelineDesc = createDefaultGraphicsPipelineStateDesc();
		pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
		pipelineDesc.NumRenderTargets = 0;
		pipelineDesc.VS = compileShader(SHADER_PATH / "ShadowVS.hlsl", "vs_5_1", &shaderBlobs[nextBlobIdx++]);

		m_shadowPass.initialize(m_pDevice, pipelineDesc, rootSignatureDesc);

		D3D12_VIEWPORT shadowViewport = createViewport((float)SHADOW_MAPS_WIDTH, (float)SHADOW_MAPS_HEIGHT);
		D3D12_RECT shadowScissorRect = createRect(SHADOW_MAPS_WIDTH, SHADOW_MAPS_HEIGHT);
		m_shadowPass.updateProperties(shadowViewport, shadowScissorRect, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		nextBlobIdx = 0;
		for (ID3DBlob*& pBlob : shaderBlobs)
			D3D12_RELEASE(pBlob);
	}

	ShadowMap& Renderer::createShadowMap(uint32_t width, uint32_t height)
	{
		ShadowMap& shadowMap = m_shadowMapPool.emplace_back();

		shadowMap.textureAllocation = m_gpuResourceManager.createTexture(width, height, 1, DXGI_FORMAT_R32_TYPELESS,
			OKAY_TEXTURE_FLAG_DEPTH | OKAY_TEXTURE_FLAG_SHADER_READ, nullptr);

		ID3D12Resource* pDXTexture = m_gpuResourceManager.getDXResource(shadowMap.textureAllocation.resourceHandle);


		DescriptorDesc shadowMapDescriptorDesc = {};
		shadowMapDescriptorDesc.type = OKAY_DESCRIPTOR_TYPE_SRV;
		shadowMapDescriptorDesc.pDXResource = pDXTexture;
		shadowMapDescriptorDesc.nullDesc = false;
		shadowMapDescriptorDesc.srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		shadowMapDescriptorDesc.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		shadowMapDescriptorDesc.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		shadowMapDescriptorDesc.srvDesc.Texture2D.MipLevels = 1;
		shadowMapDescriptorDesc.srvDesc.Texture2D.MostDetailedMip = 0;
		shadowMapDescriptorDesc.srvDesc.Texture2D.PlaneSlice = 0;
		shadowMapDescriptorDesc.srvDesc.Texture2D.ResourceMinLODClamp = 0.f;

		shadowMap.srvHandle = m_descriptorHeapStore.allocateDescriptors(m_materialTexturesDHH, (uint32_t)m_shadowMapPool.size() - 1, &shadowMapDescriptorDesc, 1).gpuHandle;


		shadowMapDescriptorDesc.type = OKAY_DESCRIPTOR_TYPE_DSV;
		shadowMapDescriptorDesc.dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		shadowMapDescriptorDesc.dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		shadowMapDescriptorDesc.dsvDesc.Texture2D.MipSlice = 0;
		shadowMapDescriptorDesc.dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		shadowMap.dsvHandle = m_descriptorHeapStore.allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &shadowMapDescriptorDesc, 1).cpuHandle;

		m_commandContext.transitionResource(pDXTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		return shadowMap;
	}

	void Renderer::trySetShadowMapData(glm::mat4 viewProjMatrix, uint32_t* pOutShadowMapIdx)
	{
		if (m_numActiveShadowMaps >= MAX_SHADOW_MAPS)
		{
			*pOutShadowMapIdx = INVALID_UINT32;
			return;
		}

		ShadowMap* pShadowMap = nullptr;
		if (m_numActiveShadowMaps >= m_shadowMapPool.size())
		{
			pShadowMap = &createShadowMap(SHADOW_MAPS_WIDTH, SHADOW_MAPS_HEIGHT);
		}
		else
		{
			pShadowMap = &m_shadowMapPool[m_numActiveShadowMaps];
		}

		pShadowMap->viewProjMatrix = viewProjMatrix;
		*pOutShadowMapIdx = m_numActiveShadowMaps++;
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
		for (uint32_t i = 0; i < (uint32_t)textures.size(); i++)
		{
			const Texture& texture = textures[i];

			Allocation textureAlloc = m_gpuResourceManager.createTexture(texture.getWidth(), texture.getHeight(), MAX_MIP_LEVELS,
				DXGI_FORMAT_R8G8B8A8_UNORM, OKAY_TEXTURE_FLAG_SHADER_READ, texture.getTextureData());

			DescriptorDesc desc = m_gpuResourceManager.createDescriptorDesc(textureAlloc, OKAY_DESCRIPTOR_TYPE_SRV, true);
			m_descriptorHeapStore.allocateDescriptors(m_materialTexturesDHH, MAX_SHADOW_MAPS + i, &desc, 1);
		}

		m_gpuResourceManager.generateMipMaps();
	}

	template<typename ComponentView>
	uint64_t Renderer::writePointLightData(ComponentView& view, uint8_t* pWriteLocation)
	{
		uint64_t bytesWritten = 0;
		for (entt::entity entity : view)
		{
			GPUPointLight* pGpuPointLight = (GPUPointLight*)(pWriteLocation + bytesWritten);
			bytesWritten += sizeof(GPUPointLight);

			auto [pointLight, transform] = view[entity];

			pGpuPointLight->colour = pointLight.colour;
			pGpuPointLight->intensity = pointLight.intensity;
			pGpuPointLight->attenuation = pointLight.attenuation;

			pGpuPointLight->position = transform.position;
		}

		return alignAddress64(bytesWritten, BUFFER_DATA_ALIGNMENT);
	}

	template<typename ComponentView>
	uint64_t Renderer::writeSpotLightData(ComponentView& view, uint8_t* pWriteLocation)
	{
		uint64_t bytesWritten = 0;
		for (entt::entity entity : view)
		{
			GPUSpotLight* pGpuSpotLight = (GPUSpotLight*)(pWriteLocation + bytesWritten);
			bytesWritten += sizeof(GPUSpotLight);

			auto [spotLight, transform] = view[entity];

			pGpuSpotLight->colour = spotLight.colour;
			pGpuSpotLight->intensity = spotLight.intensity;
			pGpuSpotLight->attenuation = spotLight.attenuation;
			pGpuSpotLight->spreadCosAngle = glm::cos(glm::radians(spotLight.spreadAngle * 0.5f));

			pGpuSpotLight->position = transform.position;
			pGpuSpotLight->direction = transform.forwardVec();

			pGpuSpotLight->viewProjMatrix = glm::transpose(
				glm::perspectiveFovLH_ZO(glm::radians(spotLight.spreadAngle), (float)SHADOW_MAPS_WIDTH, (float)SHADOW_MAPS_HEIGHT, 1.f, 3000.f) *
				transform.getViewMatrix());

			trySetShadowMapData(pGpuSpotLight->viewProjMatrix, &pGpuSpotLight->shadowMapIdx);
		}

		return alignAddress64(bytesWritten, BUFFER_DATA_ALIGNMENT);
	}

	template<typename ComponentView>
	uint64_t Renderer::writeDirLightData(ComponentView& view, uint8_t* pWriteLocation, const Transform& camTransform)
	{
		uint64_t bytesWritten = 0;
		for (entt::entity entity : view)
		{
			GPUDirectionalLight* pGpuDirLight = (GPUDirectionalLight*)(pWriteLocation + bytesWritten);
			bytesWritten += sizeof(GPUDirectionalLight);
			
			auto [directionalLight, transform] = view[entity];


			pGpuDirLight->colour = directionalLight.colour;
			pGpuDirLight->intensity = directionalLight.intensity;

			pGpuDirLight->direction = -transform.forwardVec();

			float halfShadowWorldWidth = DIR_LIGHT_SHADOW_WORLD_WIDTH * 0.5f;
			float halfShadowWorldHeight = DIR_LIGHT_SHADOW_WORLD_HEIGHT * 0.5f;

			// Snapping doesn't work ;_;

			float texelWorldWidth = DIR_LIGHT_SHADOW_WORLD_WIDTH / (float)SHADOW_MAPS_WIDTH;
			float texelWorldHeight = DIR_LIGHT_SHADOW_WORLD_HEIGHT / (float)SHADOW_MAPS_HEIGHT;

			glm::vec3 lightFocus = {};
			lightFocus.x = float(glm::floor(camTransform.position.x / texelWorldWidth) * texelWorldWidth);
			lightFocus.y = float(glm::floor(camTransform.position.y / texelWorldHeight) * texelWorldHeight);
			lightFocus.z = float(glm::floor(camTransform.position.z / texelWorldWidth) * texelWorldWidth);

			glm::vec3 lightOrigin = {};
			lightOrigin.x = float(glm::floor((lightFocus.x + pGpuDirLight->direction.x * (float)DIR_LIGHT_RANGE * 0.5f) / texelWorldWidth) * texelWorldWidth);
			lightOrigin.y = float(glm::floor((lightFocus.y + pGpuDirLight->direction.y * (float)DIR_LIGHT_RANGE * 0.5f) / texelWorldHeight) * texelWorldHeight);
			lightOrigin.z = float(glm::floor((lightFocus.z + pGpuDirLight->direction.z * (float)DIR_LIGHT_RANGE * 0.5f) / texelWorldWidth) * texelWorldWidth);

			pGpuDirLight->viewProjMatrix = glm::transpose(
				glm::orthoLH_ZO(-halfShadowWorldWidth, halfShadowWorldWidth, -halfShadowWorldHeight, halfShadowWorldHeight, 1.f, (float)DIR_LIGHT_RANGE) *
				glm::lookAtLH(lightOrigin, lightFocus, glm::vec3(0.f, 1.f, 0.f)));

			trySetShadowMapData(pGpuDirLight->viewProjMatrix, &pGpuDirLight->shadowMapIdx);

			/*
				Shadow map improvement techniques
				https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps

				Frustum Culling (extracting frustum planes)
				https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling

				// EXtracting frustum planes from matrix
				https://www.reddit.com/r/gamedev/comments/xj47t/does_glm_support_frustum_plane_extraction/
			*/
		}

		return alignAddress64(bytesWritten, BUFFER_DATA_ALIGNMENT);
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

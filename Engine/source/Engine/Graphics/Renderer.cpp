#include "Renderer.h"
#include "Engine/Resources/ResourceManager.h"

#include "Engine/Application/ImguiHelper.h"

#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_dx12.h"

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
		float farPlane = 0.f;;
	};

	struct GPUObjectData
	{
		glm::mat4 objectMatrix = glm::mat4(1.f);
		uint32_t diffuseTextureIdx = 0;
		uint32_t normalMapIdx = 0;
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
		createCommandQueue();
		createSwapChain(pFactory, window);

		// Initialize per frame resources
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			FrameResources& frame = m_frames[i];

			frame.drawGroups.list.resize(50);
			for (DrawGroup& drawGroup : frame.drawGroups.list)
			{
				drawGroup.entities.reserve(50);
			}

			frame.ringBuffer.initialize(m_pDevice, 1'000'000);
			frame.ringBuffer.map();

			frame.commandContext.initialize(m_pDevice, m_pCommandQueue);

			if (i > 0)
			{
				// Close the command lists so they can be reset the first time they're used
				// CmdCxt of frame 0 is not closed because it's used to upload resources during initialization
				frame.commandContext.execute();
			}
		}

		m_rtvIncrementSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_dsvIncrementSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		m_descriptorHeapStore.initialize(m_pDevice, 50);
		m_gpuResourceManager.initialize(m_pDevice, m_descriptorHeapStore);
		
		m_lightHandler.initiate(m_pDevice, MAX_FRAMES_IN_FLIGHT, m_gpuResourceManager, m_dxMeshes, m_descriptorHeapStore);

		fetchBackBuffersAndDSV();
		createRenderPasses(); // need to be after fetching backBuffers cuz it needs the main viewport


		// (Textures + Shadow maps)
		// At this point we don't know the real number of textures, so just setting a high upper limit
		uint32_t numTextures = 256 + LightHandler::MAX_SHADOW_MAPS + LightHandler::MAX_POINT_SHADOW_CUBES;
		m_materialTexturesDHH = m_descriptorHeapStore.createDescriptorHeap(numTextures, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);


		// In this version of Imgui, only 1 SRV is needed, it's stated that future versions will need more, but I don't see a reason to switch version atm :]
		DescriptorHeapHandle imguiHeapHandle = m_descriptorHeapStore.createDescriptorHeap(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		m_pImguiDescriptorHeap = m_descriptorHeapStore.getDXDescriptorHeap(imguiHeapHandle);

		imguiInitialize(window, m_pDevice, m_pCommandQueue, m_pImguiDescriptorHeap, MAX_FRAMES_IN_FLIGHT);
	}

	void Renderer::shutdown()
	{
		m_frames[m_currentBackBuffer].commandContext.wait();

		imguiShutdown();

		m_gpuResourceManager.shutdown();

		m_descriptorHeapStore.shutdown();
		m_lightHandler.shutdown();

		m_mainRenderPass.shutdown();

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			D3D12_RELEASE(m_frames[i].backBuffer);

			m_frames[i].commandContext.shutdown();
			m_frames[i].ringBuffer.shutdown();
		}

		D3D12_RELEASE(m_pCommandQueue);
		D3D12_RELEASE(m_pSwapChain);
		D3D12_RELEASE(m_pDevice);
	}

	void Renderer::render(const Scene& scene)
	{
		uint32_t nextFrameIdx = (m_currentBackBuffer + 1) % MAX_FRAMES_IN_FLIGHT;
		FrameResources& nextFrame = m_frames[nextFrameIdx];

		nextFrame.commandContext.wait();
		nextFrame.commandContext.reset();

		m_lightHandler.newFrame();

		D3D12_CPU_DESCRIPTOR_HANDLE currentMainRtv = {};
		preRender(&currentMainRtv);

		updateBuffers(scene);

		renderScene(scene, currentMainRtv);
		postRender();
	}

	void Renderer::preProcessResources(const ResourceManager& resourceManager)
	{
		preProcessMeshes(resourceManager.getAll<Mesh>());
		preProcessTextures(resourceManager.getAll<Texture>());
		
		m_frames[0].commandContext.execute();
	}

	void Renderer::drawDrawGroups(ID3D12GraphicsCommandList* pCommandList)
	{
		const FrameResources& frame = m_frames[m_currentBackBuffer];
		for (uint32_t i = 0; i < frame.drawGroups.numActive; i++)
		{
			const DrawGroup& drawGroup = frame.drawGroups[i];

			const DXMesh& dxMesh = m_dxMeshes[drawGroup.dxMeshId];

			pCommandList->SetGraphicsRootShaderResourceView(1, dxMesh.gpuVerticiesGVA);
			pCommandList->IASetIndexBuffer(&dxMesh.indiciesView);

			pCommandList->SetGraphicsRootShaderResourceView(2, drawGroup.objectDatasVA);

			pCommandList->DrawIndexedInstanced(dxMesh.numIndicies, (uint32_t)drawGroup.entities.size(), 0, 0, 0);
		}
	}

	void Renderer::updateBuffers(const Scene& scene)
	{
		GPURenderData mainRenderData{};
		FrameResources& frame = m_frames[m_currentBackBuffer];

		const Entity camEntity = scene.getActiveCamera();
		const Transform& camTransform = camEntity.getComponent<Transform>();
		const Camera& cameraComp = camEntity.getComponent<Camera>();

		
		frame.pointLightsGVA = m_lightHandler.writePointLightGPUData(frame.ringBuffer, frame.commandContext, scene, &mainRenderData.numPointLights);
		frame.directionalLightsGVA = m_lightHandler.writeDirLightGPUData(frame.ringBuffer, frame.commandContext, scene, &mainRenderData.numDirectionalLights);
		frame.spotLightsGVA = m_lightHandler.writeSpotLightGPUData(frame.ringBuffer, frame.commandContext, scene, &mainRenderData.numSpotLights);


		mainRenderData.cameraPos = camTransform.position;
		mainRenderData.cameraDir = camTransform.forwardVec();
		mainRenderData.viewProjMatrix = glm::transpose(cameraComp.getProjectionMatrix(m_viewport.Width, m_viewport.Height) * camTransform.getViewMatrix());

		frame.renderDataGVA = frame.ringBuffer.allocateMapped(&mainRenderData, sizeof(GPURenderData));
	}

	void Renderer::preRender(D3D12_CPU_DESCRIPTOR_HANDLE* pOutCurrentBB)
	{
		m_currentBackBuffer = (m_currentBackBuffer + 1) % MAX_FRAMES_IN_FLIGHT;
		FrameResources& frame = m_frames[m_currentBackBuffer];

		ID3D12GraphicsCommandList* pCommandList = frame.commandContext.getCommandList();

		frame.commandContext.transitionResource(frame.backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		D3D12_CPU_DESCRIPTOR_HANDLE currentBackBufferRTV = m_rtvBackBufferCPUHandle;
		currentBackBufferRTV.ptr += (uint64_t)m_currentBackBuffer * m_rtvIncrementSize;
		*pOutCurrentBB = currentBackBufferRTV;

		float testClearColor[4] = { 0.9f, 0.5f, 0.4f, 1.f };
		pCommandList->ClearRenderTargetView(currentBackBufferRTV, testClearColor, 0, nullptr);
		pCommandList->ClearDepthStencilView(frame.dsvCpuHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
	}

	void Renderer::renderScene(const Scene& scene, D3D12_CPU_DESCRIPTOR_HANDLE currentMainRtv)
	{
		FrameResources& frame = m_frames[m_currentBackBuffer];
		ID3D12GraphicsCommandList* pCommandList = frame.commandContext.getCommandList();

		assignObjectDrawGroups(scene);

		m_lightHandler.drawDepthMaps(frame.commandContext, frame.ringBuffer, frame.drawGroups.list, frame.drawGroups.numActive);

		m_mainRenderPass.bind(pCommandList, 1, &currentMainRtv, &frame.dsvCpuHandle, 1);

		ID3D12DescriptorHeap* pMaterialDXDescHeap = m_descriptorHeapStore.getDXDescriptorHeap(m_materialTexturesDHH);
		uint32_t numTotalShadowMaps = LightHandler::MAX_SHADOW_MAPS + LightHandler::MAX_POINT_SHADOW_CUBES;
		D3D12_CPU_DESCRIPTOR_HANDLE shadowMapSRVsCopyDest = pMaterialDXDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE shadowMapSRVs = m_lightHandler.getFrameShadowMapSRVCPUHandle();

		m_pDevice->CopyDescriptorsSimple(numTotalShadowMaps, shadowMapSRVsCopyDest, shadowMapSRVs, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


		pCommandList->SetDescriptorHeaps(1, &pMaterialDXDescHeap);

		pCommandList->SetGraphicsRootConstantBufferView(0, frame.renderDataGVA);
		pCommandList->SetGraphicsRootDescriptorTable(3, pMaterialDXDescHeap->GetGPUDescriptorHandleForHeapStart());
		pCommandList->SetGraphicsRootShaderResourceView(4, frame.pointLightsGVA);
		pCommandList->SetGraphicsRootShaderResourceView(5, frame.directionalLightsGVA);
		pCommandList->SetGraphicsRootShaderResourceView(6, frame.spotLightsGVA);

		drawDrawGroups(pCommandList);
	}

	void Renderer::postRender()
	{
		FrameResources& frame = m_frames[m_currentBackBuffer];
		ID3D12GraphicsCommandList* pCommandList = frame.commandContext.getCommandList();


		ImGui::Render();
		pCommandList->SetDescriptorHeaps(1, &m_pImguiDescriptorHeap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCommandList);

		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

		frame.commandContext.transitionResource(frame.backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		frame.commandContext.execute();
		m_pSwapChain->Present(0, 0);

		frame.commandContext.signal();
		frame.ringBuffer.jumpToStart();
	}

	void Renderer::assignObjectDrawGroups(const Scene& scene)
	{
		FrameResources& frame = m_frames[m_currentBackBuffer];

		auto meshRendererView = scene.getRegistry().view<MeshRenderer, Transform>();

		for (DrawGroup& drawGroup : frame.drawGroups.list)
		{
			drawGroup.entities.clear();
		}

		frame.drawGroups.numActive = 0;
		for (entt::entity entity : meshRendererView)
		{
			const MeshRenderer& meshRenderer = meshRendererView.get<MeshRenderer>(entity);

			uint32_t drawGroupIdx = INVALID_UINT32;
			for (uint32_t i = 0; i < frame.drawGroups.numActive; i++)
			{
				if (frame.drawGroups[i].dxMeshId == meshRenderer.meshID)
				{
					drawGroupIdx = i;
					break;
				}
			}

			if (drawGroupIdx == INVALID_UINT32)
			{
				if (frame.drawGroups.numActive >= (uint32_t)frame.drawGroups.list.size())
				{
					frame.drawGroups.list.resize(frame.drawGroups.list.size() + 20);
				}

				drawGroupIdx = frame.drawGroups.numActive ++;
				frame.drawGroups[drawGroupIdx].dxMeshId = meshRenderer.meshID;
			}

			frame.drawGroups[drawGroupIdx].entities.emplace_back(entity);
		}


		// Upload ObjectDatas
		D3D12_GPU_VIRTUAL_ADDRESS drawGroupObjectDatasVA = frame.ringBuffer.getCurrentGPUAddress();

		for (uint32_t i = 0; i < frame.drawGroups.numActive; i++)
		{
			DrawGroup& drawGroup = frame.drawGroups[i];

			for (entt::entity entity : drawGroup.entities)
			{
				auto [meshRenderer, transform] = meshRendererView[entity];

				GPUObjectData* pObjectData = (GPUObjectData*)frame.ringBuffer.getMappedPtr();
				pObjectData->objectMatrix = glm::transpose(transform.getMatrix());
				pObjectData->diffuseTextureIdx = meshRenderer.diffuseTextureID;
				pObjectData->normalMapIdx = meshRenderer.normalMapID;

				frame.ringBuffer.offsetMappedPtr(sizeof(GPUObjectData));
			}

			drawGroup.objectDatasVA = drawGroupObjectDatasVA;
			drawGroupObjectDatasVA += drawGroup.entities.size() * sizeof(GPUObjectData);
		}

		frame.ringBuffer.alignOffset();
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

	void Renderer::createCommandQueue()
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		DX_CHECK(m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue)));
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
		swapChainDesc.BufferCount = MAX_FRAMES_IN_FLIGHT;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapChainDesc.Flags = 0;

		DX_CHECK(pFactory2->CreateSwapChainForHwnd(m_pCommandQueue, window.getHWND(), &swapChainDesc, nullptr, nullptr, &m_pSwapChain));

		D3D12_RELEASE(pFactory2);
	}

	void Renderer::fetchBackBuffersAndDSV()
	{
		TextureDescription depthTextureDesc = {};
		depthTextureDesc.mipLevels = 1;
		depthTextureDesc.arraySize = 1;
		depthTextureDesc.format = DXGI_FORMAT_D32_FLOAT;
		depthTextureDesc.flags = OKAY_TEXTURE_FLAG_DEPTH;

		DescriptorDesc backBufferRTVs[MAX_FRAMES_IN_FLIGHT] = {};

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			FrameResources& frame = m_frames[i];

			DX_CHECK(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&frame.backBuffer)));

			backBufferRTVs[i].type = OKAY_DESCRIPTOR_TYPE_RTV;
			backBufferRTVs[i].pDXResource = frame.backBuffer;


			// Create depth stencil texture & descriptor
			D3D12_RESOURCE_DESC resourceDesc = frame.backBuffer->GetDesc();

			depthTextureDesc.width = (uint32_t)resourceDesc.Width;
			depthTextureDesc.height = resourceDesc.Height;

			Allocation dsAllocation = m_gpuResourceManager.createTexture(depthTextureDesc, nullptr, nullptr);

			DescriptorDesc dsvDesc = m_gpuResourceManager.createDescriptorDesc(dsAllocation, OKAY_DESCRIPTOR_TYPE_DSV, true);
			frame.dsvCpuHandle = m_descriptorHeapStore.allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &dsvDesc, 1).cpuHandle;

			m_frames[0].commandContext.transitionResource(dsAllocation.pDXResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		}

		m_rtvBackBufferCPUHandle = m_descriptorHeapStore.allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, backBufferRTVs, MAX_FRAMES_IN_FLIGHT).cpuHandle;


		// Find viewport & scissor rect
		D3D12_RESOURCE_DESC backBufferDesc = m_frames[0].backBuffer->GetDesc();

		m_viewport = createViewport((float)backBufferDesc.Width, (float)backBufferDesc.Height);
		m_scissorRect = createRect((uint32_t)backBufferDesc.Width, backBufferDesc.Height);
	}

	void Renderer::createRenderPasses()
	{
		D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
		samplers[0] = createDefaultStaticPointSamplerDesc();
		samplers[0].ShaderRegister = 0;

		samplers[1] = samplers[0];
		samplers[1].Filter = D3D12_FILTER_ANISOTROPIC;
		samplers[1].MaxAnisotropy = 4;
		samplers[1].ShaderRegister = 1;

		samplers[2] = samplers[0];
		samplers[2].Filter = D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;;
		samplers[2].ShaderRegister = 2;

		std::vector<D3D12_ROOT_PARAMETER> rootParams = {};
		rootParams.reserve(32);

		ID3DBlob* shaderBlobs[5] = {};
		uint32_t nextBlobIdx = 0;


		// Main Render Pass

		rootParams.emplace_back(createRootParamCBV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Main Render Data (GPURenderData)
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Verticies SRV
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 1, 0)); // Object datas (GPUObjcetData)

		// At this point we don't know the real number of textures, so just setting a high upper limit
		D3D12_DESCRIPTOR_RANGE textureDescriptorRanges[3] =
		{
			createRangeSRV(6, 2, LightHandler::MAX_SHADOW_MAPS, 0),
			createRangeSRV(7, 3, LightHandler::MAX_POINT_SHADOW_CUBES, LightHandler::MAX_SHADOW_MAPS),
			createRangeSRV(2, 1, 256, LightHandler::MAX_SHADOW_MAPS + LightHandler::MAX_POINT_SHADOW_CUBES),
		};
		// Textures + Shadow maps
		rootParams.emplace_back(createRootParamTable(D3D12_SHADER_VISIBILITY_PIXEL, textureDescriptorRanges, _countof(textureDescriptorRanges)));

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

		RingBuffer meshDataUploadBuffer;
		meshDataUploadBuffer.initialize(m_pDevice, 50'000'000);
		meshDataUploadBuffer.map();

		Resource verticiesR = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, verticiesResourceSize);
		Resource indiciesR = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, indiciesResourceSize);

		m_dxMeshes.resize(meshes.size());

		for (uint32_t i = 0; i < (uint32_t)meshes.size(); i++)
		{
			const std::vector<Vertex>& verticies = meshes[i].getMeshData().verticies;
			const std::vector<uint32_t>& indicies = meshes[i].getMeshData().indicies;

			Allocation verticiesAlloc = m_gpuResourceManager.allocateInto(verticiesR, OKAY_RESOURCE_APPEND, sizeof(Vertex), (uint32_t)verticies.size(), verticies.data(), &meshDataUploadBuffer, &m_frames[0].commandContext);
			Allocation indiciesAlloc = m_gpuResourceManager.allocateInto(indiciesR, OKAY_RESOURCE_APPEND, sizeof(uint32_t), (uint32_t)indicies.size(), indicies.data(), &meshDataUploadBuffer, &m_frames[0].commandContext);

			m_dxMeshes[i].gpuVerticiesGVA = m_gpuResourceManager.getVirtualAddress(verticiesAlloc);

			m_dxMeshes[i].indiciesView.BufferLocation = m_gpuResourceManager.getVirtualAddress(indiciesAlloc);
			m_dxMeshes[i].indiciesView.SizeInBytes = (uint32_t)indiciesAlloc.elementSize * indiciesAlloc.numElements;
			m_dxMeshes[i].indiciesView.Format = DXGI_FORMAT_R32_UINT;

			m_dxMeshes[i].numIndicies = (uint32_t)indicies.size();
		}

		m_frames[0].commandContext.transitionResource(indiciesR.pDXResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		m_frames[0].commandContext.flush();

		meshDataUploadBuffer.shutdown();
	}

	void Renderer::preProcessTextures(const std::vector<Texture>& textures)
	{
		TextureDescription textureDesc = {};
		textureDesc.mipLevels = MAX_MIP_LEVELS;
		textureDesc.arraySize = 1;
		textureDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.flags = OKAY_TEXTURE_FLAG_SHADER_READ;

		uint32_t numTotalShadowMaps = LightHandler::MAX_SHADOW_MAPS + LightHandler::MAX_POINT_SHADOW_CUBES;
		for (uint32_t i = 0; i < (uint32_t)textures.size(); i++)
		{
			const Texture& texture = textures[i];

			textureDesc.width = texture.getWidth();
			textureDesc.height = texture.getHeight();

			Allocation textureAlloc = m_gpuResourceManager.createTexture(textureDesc, texture.getTextureData(), &m_frames[0].commandContext);

			DescriptorDesc desc = m_gpuResourceManager.createDescriptorDesc(textureAlloc, OKAY_DESCRIPTOR_TYPE_SRV, true);
			m_descriptorHeapStore.allocateDescriptors(m_materialTexturesDHH, numTotalShadowMaps + i, &desc, 1);
		}

		m_frames[0].commandContext.flush();
		m_gpuResourceManager.generateMipMaps(m_pCommandQueue);
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

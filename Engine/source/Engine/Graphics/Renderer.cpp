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
		m_commandContext.initialize(m_pDevice, D3D12_COMMAND_LIST_TYPE_DIRECT);

		createSwapChain(pFactory, window);

		m_rtvIncrementSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_dsvIncrementSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		m_ringBuffer.initialize(m_pDevice, 50'000'000);
		m_ringBuffer.map();

		m_descriptorHeapStore.initialize(m_pDevice, 50);
		m_gpuResourceManager.initialize(m_pDevice, m_commandContext, m_ringBuffer, m_descriptorHeapStore);
		
		m_lightHandler.initiate(m_pDevice, m_gpuResourceManager, m_commandContext, m_dxMeshes);

		fetchBackBuffersAndDSV();
		createRenderPasses(); // need to be after fetching backBuffers cuz it needs the main viewport

		m_activeDrawGroups = 0;
		m_drawGroups.resize(10);
		for (DrawGroup& drawGroup : m_drawGroups)
		{
			drawGroup.entities.reserve(50);
		}

		Resource batchVertices = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, sizeof(Vertex) * 200'000);
		Resource batchIndices = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, sizeof(uint32_t) * 1'000'000);

		batchVertices.pDXResource->SetName(L"BatchedVertices");
		batchIndices.pDXResource->SetName(L"BatchedIndices");

		m_commandContext.transitionResource(batchIndices.pDXResource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);

		m_batchedVertices = m_gpuResourceManager.allocateInto(batchVertices, 0, 0, 1, nullptr);
		m_batchedIndices = m_gpuResourceManager.allocateInto(batchIndices, 0, 0, 1, nullptr);


		// (Textures + Shadow maps)
		// At this point we don't know the real number of textures, so just setting a high upper limit
		uint32_t numTextures = 256 + LightHandler::MAX_SHADOW_MAPS + LightHandler::MAX_POINT_SHADOW_CUBES;
		m_materialTexturesDHH = m_descriptorHeapStore.createDescriptorHeap(numTextures, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_lightHandler.setShadowMapDescritprHeap(m_descriptorHeapStore, m_materialTexturesDHH, 0);


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

		m_ringBuffer.unmap();
		m_ringBuffer.shutdown();

		m_descriptorHeapStore.shutdown();
		m_lightHandler.shutdown();

		m_mainRenderPass.shutdown();

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

		//m_commandContext.flush();
		//m_ringBuffer.resize(100'000);
	}

	void Renderer::drawDrawGroups(ID3D12GraphicsCommandList* pCommandList)
	{
		for (uint32_t i = 0; i < m_activeDrawGroups; i++)
		{
			const DrawGroup& drawGroup = m_drawGroups[i];

			pCommandList->SetGraphicsRootShaderResourceView(1, drawGroup.verticiesGVA);
			pCommandList->IASetIndexBuffer(&drawGroup.indiciesView);

			pCommandList->SetGraphicsRootShaderResourceView(2, drawGroup.objectDatasVA);
			pCommandList->SetGraphicsRootShaderResourceView(3, drawGroup.batchedObjectDataIndicies);

			pCommandList->DrawIndexedInstanced(drawGroup.numIndicies, 1, 0, 0, 0);
		}
	}

	void Renderer::updateBuffers(const Scene& scene)
	{
		GPURenderData mainRenderData{};

		const Entity camEntity = scene.getActiveCamera();
		const Transform& camTransform = camEntity.getComponent<Transform>();
		const Camera& cameraComp = camEntity.getComponent<Camera>();

		
		m_lightHandler.newFrame();

		m_pointLightsGVA = m_lightHandler.writePointLightGPUData(scene, m_ringBuffer, &mainRenderData.numPointLights);
		m_directionalLightsGVA = m_lightHandler.writeDirLightGPUData(scene, m_ringBuffer, &mainRenderData.numDirectionalLights);
		m_spotLightsGVA = m_lightHandler.writeSpotLightGPUData(scene, m_ringBuffer, &mainRenderData.numSpotLights);


		mainRenderData.cameraPos = camTransform.position;
		mainRenderData.cameraDir = camTransform.forwardVec();
		mainRenderData.viewProjMatrix = glm::transpose(cameraComp.getProjectionMatrix(m_viewport.Width, m_viewport.Height) * camTransform.getViewMatrix());

		m_renderDataGVA = m_ringBuffer.allocateMapped(&mainRenderData, sizeof(GPURenderData));
	}

	void Renderer::preRender(D3D12_CPU_DESCRIPTOR_HANDLE* pOutCurrentBB)
	{
		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();

		m_currentBackBuffer = (m_currentBackBuffer + 1) % NUM_BACKBUFFERS;
		m_commandContext.transitionResource(m_backBuffers[m_currentBackBuffer], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		D3D12_CPU_DESCRIPTOR_HANDLE currentBackBufferRTV = m_rtvBackBufferCPUHandle;
		currentBackBufferRTV.ptr += (uint64_t)m_currentBackBuffer * m_rtvIncrementSize;
		*pOutCurrentBB = currentBackBufferRTV;

		float testClearColor[4] = { 0.9f, 0.5f, 0.4f, 1.f };
		pCommandList->ClearRenderTargetView(currentBackBufferRTV, testClearColor, 0, nullptr);
		pCommandList->ClearDepthStencilView(m_mainDsvCpuHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
	}

	void Renderer::renderScene(const Scene& scene, D3D12_CPU_DESCRIPTOR_HANDLE currentMainRtv)
	{
		assignObjectDrawGroups(scene);

		m_lightHandler.drawDepthMaps(m_drawGroups, m_activeDrawGroups, m_ringBuffer);

		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();
		m_mainRenderPass.bind(pCommandList, 1, &currentMainRtv, &m_mainDsvCpuHandle, 1);

		ID3D12DescriptorHeap* pMaterialDXDescHeap = m_descriptorHeapStore.getDXDescriptorHeap(m_materialTexturesDHH);
		pCommandList->SetDescriptorHeaps(1, &pMaterialDXDescHeap);

		pCommandList->SetGraphicsRootConstantBufferView(0, m_renderDataGVA);
		pCommandList->SetGraphicsRootDescriptorTable(4, pMaterialDXDescHeap->GetGPUDescriptorHandleForHeapStart());
		pCommandList->SetGraphicsRootShaderResourceView(5, m_pointLightsGVA);
		pCommandList->SetGraphicsRootShaderResourceView(6, m_directionalLightsGVA);
		pCommandList->SetGraphicsRootShaderResourceView(7, m_spotLightsGVA);

		drawDrawGroups(pCommandList);
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
		m_pSwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

		m_commandContext.wait();
		m_commandContext.reset();

		m_ringBuffer.jumpToStart();
	}

	void Renderer::assignObjectDrawGroups(const Scene& scene)
	{
		auto meshRendererView = scene.getRegistry().view<MeshRenderer, Transform>();

		for (uint32_t i = 0; i < m_activeDrawGroups; i++)
		{
			m_drawGroups[i].entities.clear();
		}

		// Temp
		m_activeDrawGroups = 1;
		uint32_t objects = 0;
		for (entt::entity entity : meshRendererView)
		{
			const MeshRenderer& meshRenderer = meshRendererView.get<MeshRenderer>(entity);
			m_drawGroups[0].entities.emplace_back(entity);
		}


		// Upload ObjectDatas
		for (uint32_t i = 0; i < m_activeDrawGroups; i++)
		{
			DrawGroup& drawGroup = m_drawGroups[i];

			GPUObjectData* pObjectDatas = (GPUObjectData*)m_ringBuffer.getMappedPtr();
			uint32_t* pObjDataIndicies = (uint32_t*)(pObjectDatas + drawGroup.entities.size());

			uint32_t numVertices = 0;
			drawGroup.numIndicies = 0;

			for (uint32_t k = 0; k < (uint32_t)drawGroup.entities.size(); k++)
			{
				auto [meshRenderer, transform] = meshRendererView[drawGroup.entities[k]];

				GPUObjectData* pObjectData = pObjectDatas + k;
				pObjectData->objectMatrix = glm::transpose(transform.getMatrix());
				pObjectData->diffuseTextureIdx = meshRenderer.diffuseTextureID;
				pObjectData->normalMapIdx = meshRenderer.normalMapID;

				const DXMesh& dxMesh = m_dxMeshes[meshRenderer.meshID];
				for (uint32_t j = 0; j < dxMesh.numVerticies; j++)
				{
					*(pObjDataIndicies + j) = k;
				}

				pObjDataIndicies += dxMesh.numVerticies;

				drawGroup.numIndicies += (uint32_t)dxMesh.indices.size();
				numVertices += dxMesh.numVerticies;
			}

			drawGroup.objectDatasVA = m_ringBuffer.getCurrentGPUAddress();
			m_ringBuffer.offsetMappedPtr(drawGroup.entities.size() * sizeof(GPUObjectData));

			drawGroup.batchedObjectDataIndicies = m_ringBuffer.getCurrentGPUAddress();
			m_ringBuffer.offsetMappedPtr(numVertices * sizeof(uint32_t));
		}



		// GPU copy vertex & index data from meshes into batched verticies & indicies dxResources

		static std::vector<uint32_t> s_indices;


		m_batchedVertices.resourceOffset = 0;
		m_batchedIndices.resourceOffset = 0;

		m_commandContext.transitionResource(m_batchedIndices.pDXResource, D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);

		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();
		for (uint32_t i = 0; i < m_activeDrawGroups; i++)
		{
			s_indices.clear();

			DrawGroup& drawGroup = m_drawGroups[i];

			drawGroup.verticiesGVA = m_batchedVertices.pDXResource->GetGPUVirtualAddress() + m_batchedVertices.resourceOffset;

			for (uint32_t k = 0; k < (uint32_t)drawGroup.entities.size(); k++)
			{
				const MeshRenderer& meshRenderer = meshRendererView.get<MeshRenderer>(drawGroup.entities[k]);

				const Allocation& verticesAlloc = m_dxMeshes[meshRenderer.meshID].verticiesAlloc;
				uint64_t verticiesBytes = verticesAlloc.elementSize * verticesAlloc.numElements;

				pCommandList->CopyBufferRegion(m_batchedVertices.pDXResource, m_batchedVertices.resourceOffset, verticesAlloc.pDXResource, verticesAlloc.resourceOffset, verticiesBytes);

				m_batchedVertices.resourceOffset += verticiesBytes;
				OKAY_ASSERT(m_batchedVertices.resourceOffset <= m_batchedVertices.elementSize);
			}


			drawGroup.indiciesView.BufferLocation = m_batchedIndices.pDXResource->GetGPUVirtualAddress() + m_batchedIndices.resourceOffset;
			drawGroup.indiciesView.SizeInBytes = drawGroup.numIndicies * sizeof(uint32_t);
			drawGroup.indiciesView.Format = DXGI_FORMAT_R32_UINT;

			uint32_t indexOffset = 0;
			for (uint32_t k = 0; k < (uint32_t)drawGroup.entities.size(); k++)
			{
				const MeshRenderer& meshRenderer = meshRendererView.get<MeshRenderer>(drawGroup.entities[k]);

				const DXMesh& dxMesh = m_dxMeshes[meshRenderer.meshID];
				const Allocation& indiciesAlloc = dxMesh.indiciesAlloc;
				uint64_t indiciesBytes = indiciesAlloc.elementSize * indiciesAlloc.numElements;

				for (uint32_t j = 0; j < (uint32_t)dxMesh.indices.size(); j++)
				{
					s_indices.emplace_back(indexOffset + dxMesh.indices[j]);
				}

				indexOffset += (uint32_t)dxMesh.numVerticies;
			}

			uint32_t indicesByteSize = s_indices.size() * sizeof(uint32_t);

			memcpy(m_ringBuffer.getMappedPtr(), s_indices.data(), indicesByteSize);
			pCommandList->CopyBufferRegion(m_batchedIndices.pDXResource, m_batchedIndices.resourceOffset,  m_ringBuffer.getDXResource(), m_ringBuffer.getOffset(), indicesByteSize);

			m_ringBuffer.offsetMappedPtr(indicesByteSize);

			m_batchedIndices.resourceOffset += indicesByteSize;
			OKAY_ASSERT(m_batchedIndices.resourceOffset <= m_batchedIndices.elementSize);
		}
		
		m_ringBuffer.alignOffset();

		m_commandContext.transitionResource(m_batchedIndices.pDXResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
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
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

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
		Allocation dsAllocation = m_gpuResourceManager.createTexture((uint32_t)resourceDesc.Width, resourceDesc.Height, 1, 1, DXGI_FORMAT_D32_FLOAT, OKAY_TEXTURE_FLAG_DEPTH, nullptr);

		DescriptorDesc dsvDesc = m_gpuResourceManager.createDescriptorDesc(dsAllocation, OKAY_DESCRIPTOR_TYPE_DSV, true);
		m_mainDsvCpuHandle = m_descriptorHeapStore.allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &dsvDesc, 1).cpuHandle;

		m_commandContext.transitionResource(dsAllocation.pDXResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		// Find viewport & scissor rect
		D3D12_RESOURCE_DESC backBufferDesc = m_backBuffers[0]->GetDesc();

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
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Verticies SRV (Vertex)
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 1, 0)); // Object datas (GPUObjcetData)
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 8, 0)); // Batched objData indicies (uint32_t)

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

		Resource verticiesR = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, verticiesResourceSize);
		Resource indiciesR = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, indiciesResourceSize);

		m_dxMeshes.resize(meshes.size());

		for (uint32_t i = 0; i < (uint32_t)meshes.size(); i++)
		{
			DXMesh& dxMesh = m_dxMeshes[i];

			const std::vector<Vertex>& verticies = meshes[i].getMeshData().verticies;
			const std::vector<uint32_t>& indicies = meshes[i].getMeshData().indicies;

			dxMesh.verticiesAlloc = m_gpuResourceManager.allocateInto(verticiesR, OKAY_RESOURCE_APPEND, sizeof(Vertex), (uint32_t)verticies.size(), verticies.data());
			dxMesh.indiciesAlloc = m_gpuResourceManager.allocateInto(indiciesR, OKAY_RESOURCE_APPEND, sizeof(uint32_t), (uint32_t)indicies.size(), indicies.data());

			dxMesh.gpuVerticiesGVA = m_gpuResourceManager.getVirtualAddress(dxMesh.verticiesAlloc);

			dxMesh.indiciesView.BufferLocation = m_gpuResourceManager.getVirtualAddress(dxMesh.indiciesAlloc);
			dxMesh.indiciesView.SizeInBytes = (uint32_t)(dxMesh.indiciesAlloc.elementSize * dxMesh.indiciesAlloc.numElements);
			dxMesh.indiciesView.Format = DXGI_FORMAT_R32_UINT;

			dxMesh.indices = indicies;
			dxMesh.numVerticies = (uint32_t)verticies.size();
		}

		m_commandContext.transitionResource(indiciesR.pDXResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
	}

	void Renderer::preProcessTextures(const std::vector<Texture>& textures)
	{
		uint32_t numTotalShadowMaps = LightHandler::MAX_SHADOW_MAPS + LightHandler::MAX_POINT_SHADOW_CUBES;
		for (uint32_t i = 0; i < (uint32_t)textures.size(); i++)
		{
			const Texture& texture = textures[i];

			Allocation textureAlloc = m_gpuResourceManager.createTexture(texture.getWidth(), texture.getHeight(), MAX_MIP_LEVELS, 1,
				DXGI_FORMAT_R8G8B8A8_UNORM, OKAY_TEXTURE_FLAG_SHADER_READ, texture.getTextureData());

			DescriptorDesc desc = m_gpuResourceManager.createDescriptorDesc(textureAlloc, OKAY_DESCRIPTOR_TYPE_SRV, true);
			m_descriptorHeapStore.allocateDescriptors(m_materialTexturesDHH, numTotalShadowMaps + i, &desc, 1);
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

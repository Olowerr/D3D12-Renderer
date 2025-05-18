#include "LightHandler.h"

namespace Okay
{
	static D3D12_RESOURCE_BARRIER s_shadowMapBarriers[LightHandler::MAX_SHADOW_MAPS + LightHandler::MAX_POINT_SHADOW_CUBES] = {};

	struct GPUShadowMapCubeData
	{
		glm::mat4 matrices[6] = {};
		glm::vec3 lightPos = glm::vec3(0.f);
		float farPlane = 0.f;
	};

	struct GPUPointLight
	{
		uint32_t shadowMapIdx = INVALID_UINT32;
		float farPlane = 0.f;

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

	void LightHandler::initiate(ID3D12Device* pDevice, GPUResourceManager& gpuResourceManager, CommandContext& commandContext, RingBuffer& ringBuffer, const std::vector<DXMesh>& dxMeshes)
	{
		m_pDevice = pDevice;
		m_pGpuResourceManager = &gpuResourceManager;
		m_pCommandContext = &commandContext;
		m_pRingBuffer = &ringBuffer;
		m_pDxMeshes = &dxMeshes;

		createRenderPasses();
	}

	void LightHandler::shutdown()
	{
		m_shadowPass.shutdown();
		m_shadowPassPointLights.shutdown();
	}
	
	void LightHandler::setShadowMapDescritprHeap(DescriptorHeapStore& store, DescriptorHeapHandle shadowMapHandle, uint32_t offset)
	{
		m_pDescriptorHeapStore = &store;
		m_shadowMapDHH = shadowMapHandle;
		m_shadowDescriptorsOffset = offset;
	}
	
	uint64_t LightHandler::writePointLightGPUData(const Scene& scene, uint8_t* pWriteLocation, uint32_t* pOutNumPointLights)
	{
		static const glm::vec3 CUBE_MAP_DIRECTIONS[6] =
		{
			glm::vec3(-1.f, 0.f, 0.f),
			glm::vec3(1.f, 0.f, 0.f),

			glm::vec3(0.f, -1.f, 0.f),
			glm::vec3(0.f, 1.f, 0.f),

			glm::vec3(0.f, 0.f, -1.f),
			glm::vec3(0.f, 0.f, 1.f),
		};

		auto pointLightView = scene.getRegistry().view<PointLight, Transform>();

		uint64_t bytesWritten = 0;
		for (entt::entity entity : pointLightView)
		{
			GPUPointLight* pGpuPointLight = (GPUPointLight*)(pWriteLocation + bytesWritten);
			bytesWritten += sizeof(GPUPointLight);

			auto [pointLight, transform] = pointLightView[entity];

			pGpuPointLight->colour = pointLight.colour;
			pGpuPointLight->intensity = pointLight.intensity;
			pGpuPointLight->attenuation = pointLight.attenuation;

			pGpuPointLight->position = transform.position;

			if (!pointLight.shadowSource)
			{
				pGpuPointLight->shadowMapIdx = INVALID_UINT32;
				continue;
			}

			pGpuPointLight->farPlane = 3000.f;

			glm::mat4 projMatrix = glm::perspectiveFovLH_ZO(glm::half_pi<float>(), (float)SHADOW_MAPS_WIDTH, (float)SHADOW_MAPS_HEIGHT, 1.f, pGpuPointLight->farPlane);
			glm::mat4 viewProjMatrices[6] = {};
			for (uint32_t i = 0; i < 6; i++)
			{
				glm::vec3 upVector = glm::vec3(0.f, 1.f, 0.f);
				if (i == 2)
				{
					upVector = glm::vec3(0.f, 0.f, -1.f);
				}
				else if (i == 3)
				{
					upVector = glm::vec3(0.f, 0.f, 1.f);
				}


				viewProjMatrices[i] = glm::transpose(projMatrix * glm::lookAt(transform.position, transform.position + CUBE_MAP_DIRECTIONS[i], upVector));
			}

			trySetShadowMapCubeData(viewProjMatrices, transform.position, &pGpuPointLight->shadowMapIdx);
		}

		return alignAddress64(bytesWritten, BUFFER_DATA_ALIGNMENT);
	}
	
	uint64_t LightHandler::writeDirLightGPUData(const Scene& scene, uint8_t* pWriteLocation, uint32_t* pOutNumDirLights)
	{
		auto dirLightView = scene.getRegistry().view<DirectionalLight, Transform>();
		const Transform& camTransform = scene.getActiveCamera().getComponent<Transform>();

		uint64_t bytesWritten = 0;
		for (entt::entity entity : dirLightView)
		{
			GPUDirectionalLight* pGpuDirLight = (GPUDirectionalLight*)(pWriteLocation + bytesWritten);
			bytesWritten += sizeof(GPUDirectionalLight);

			auto [directionalLight, transform] = dirLightView[entity];


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
	
	uint64_t LightHandler::writeSpotLightGPUData(const Scene& scene, uint8_t* pWriteLocation, uint32_t* pOutNumSpotLights)
	{
		auto spotLightView = scene.getRegistry().view<SpotLight, Transform>();

		uint64_t bytesWritten = 0;
		for (entt::entity entity : spotLightView)
		{
			GPUSpotLight* pGpuSpotLight = (GPUSpotLight*)(pWriteLocation + bytesWritten);
			bytesWritten += sizeof(GPUSpotLight);

			auto [spotLight, transform] = spotLightView[entity];

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
	
	void LightHandler::preDepthMapRender()
	{
		ID3D12GraphicsCommandList* pCommandList = m_pCommandContext->getCommandList();

		for (uint32_t i = 0; i < m_shadowMapPool.m_numActive; i++)
		{
			s_shadowMapBarriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			s_shadowMapBarriers[i].Transition.pResource = m_pGpuResourceManager->getDXResource(m_shadowMapPool[i].textureAllocation.resourceHandle);
			s_shadowMapBarriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			s_shadowMapBarriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			s_shadowMapBarriers[i].Transition.Subresource = 0;
			s_shadowMapBarriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		}
		for (uint32_t i = 0; i < m_shadowMapCubePool.m_numActive; i++)
		{
			uint32_t barrierIdx = i + m_shadowMapPool.m_numActive;
			s_shadowMapBarriers[barrierIdx].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			s_shadowMapBarriers[barrierIdx].Transition.pResource = m_pGpuResourceManager->getDXResource(m_shadowMapCubePool[i].textureAllocation.resourceHandle);
			s_shadowMapBarriers[barrierIdx].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			s_shadowMapBarriers[barrierIdx].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			s_shadowMapBarriers[barrierIdx].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			s_shadowMapBarriers[barrierIdx].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		}

		pCommandList->ResourceBarrier(m_shadowMapPool.m_numActive + m_shadowMapCubePool.m_numActive, s_shadowMapBarriers);

		for (uint32_t i = 0; i < m_shadowMapPool.m_numActive; i++)
		{
			pCommandList->ClearDepthStencilView(m_shadowMapPool[i].dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
		}
		for (uint32_t i = 0; i < m_shadowMapCubePool.m_numActive; i++)
		{
			pCommandList->ClearDepthStencilView(m_shadowMapCubePool[i].dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
		}
	}

	void LightHandler::drawDepthMaps(const std::vector<DrawGroup>& drawGroups, uint32_t numActiveDrawGroups)
	{
		preDepthMapRender();

		uint8_t* pMappedRingBuffer = m_pRingBuffer->map();
		uint64_t ringBufferOffset = 0;

		ID3D12GraphicsCommandList* pCommandList = m_pCommandContext->getCommandList();

		m_shadowPass.bindBase(pCommandList);

		for (uint32_t i = 0; i < m_shadowMapPool.m_numActive; i++)
		{
			const ShadowMap& shadowMap = m_shadowMapPool[i];

			memcpy(pMappedRingBuffer + ringBufferOffset, &shadowMap.viewProjMatrix, sizeof(glm::mat4));

			D3D12_GPU_VIRTUAL_ADDRESS lightCamBuffer = m_pRingBuffer->getCurrentGPUAddress() + ringBufferOffset;
			pCommandList->SetGraphicsRootConstantBufferView(0, lightCamBuffer);

			m_shadowPass.bindRTVs(pCommandList, 0, nullptr, &shadowMap.dsvHandle, 1);

			for (uint32_t i = 0; i < numActiveDrawGroups; i++)
			{
				const DrawGroup& drawGroup = drawGroups[i];

				const DXMesh& dxMesh = (*m_pDxMeshes)[drawGroup.dxMeshId];

				pCommandList->SetGraphicsRootShaderResourceView(1, dxMesh.gpuVerticiesGVA);
				pCommandList->IASetIndexBuffer(&dxMesh.indiciesView);

				pCommandList->SetGraphicsRootShaderResourceView(2, drawGroup.objectDatasVA);

				pCommandList->DrawIndexedInstanced(dxMesh.numIndicies, (uint32_t)drawGroup.entities.size(), 0, 0, 0);
			}

			ringBufferOffset += alignAddress64(sizeof(glm::mat4), BUFFER_DATA_ALIGNMENT);

			s_shadowMapBarriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			s_shadowMapBarriers[i].Transition.pResource = m_pGpuResourceManager->getDXResource(m_shadowMapPool[i].textureAllocation.resourceHandle);
			s_shadowMapBarriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			s_shadowMapBarriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			s_shadowMapBarriers[i].Transition.Subresource = 0;
			s_shadowMapBarriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		}


		m_shadowPassPointLights.bindBase(pCommandList);

		for (uint32_t i = 0; i < m_shadowMapCubePool.m_numActive; i++)
		{
			const ShadowMapCube& shadowMapCube = m_shadowMapCubePool[i];

			GPUShadowMapCubeData* pShadowMapData = (GPUShadowMapCubeData*)(pMappedRingBuffer + ringBufferOffset);
			memcpy(pShadowMapData->matrices, shadowMapCube.viewProjMatrices, sizeof(glm::mat4) * 6);
			pShadowMapData->lightPos = shadowMapCube.lightPos;
			pShadowMapData->farPlane = 3000.f;

			D3D12_GPU_VIRTUAL_ADDRESS lightCamBuffer = m_pRingBuffer->getCurrentGPUAddress() + ringBufferOffset;
			pCommandList->SetGraphicsRootConstantBufferView(0, lightCamBuffer);

			m_shadowPassPointLights.bindRTVs(pCommandList, 0, nullptr, &shadowMapCube.dsvHandle, 6);

			for (uint32_t i = 0; i < numActiveDrawGroups; i++)
			{
				const DrawGroup& drawGroup = drawGroups[i];

				const DXMesh& dxMesh = (*m_pDxMeshes)[drawGroup.dxMeshId];

				pCommandList->SetGraphicsRootShaderResourceView(1, dxMesh.gpuVerticiesGVA);
				pCommandList->IASetIndexBuffer(&dxMesh.indiciesView);

				pCommandList->SetGraphicsRootShaderResourceView(2, drawGroup.objectDatasVA);

				pCommandList->DrawIndexedInstanced(dxMesh.numIndicies, (uint32_t)drawGroup.entities.size(), 0, 0, 0);
			}

			ringBufferOffset += alignAddress64(sizeof(GPUShadowMapCubeData), BUFFER_DATA_ALIGNMENT);

			s_shadowMapBarriers[i + m_shadowMapPool.getSize()].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			s_shadowMapBarriers[i + m_shadowMapPool.getSize()].Transition.pResource = m_pGpuResourceManager->getDXResource(m_shadowMapCubePool[i].textureAllocation.resourceHandle);
			s_shadowMapBarriers[i + m_shadowMapPool.getSize()].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			s_shadowMapBarriers[i + m_shadowMapPool.getSize()].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			s_shadowMapBarriers[i + m_shadowMapPool.getSize()].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			s_shadowMapBarriers[i + m_shadowMapPool.getSize()].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		}

		pCommandList->ResourceBarrier(m_shadowMapPool.m_numActive + m_shadowMapCubePool.m_numActive, s_shadowMapBarriers);

		m_pRingBuffer->unmap(ringBufferOffset);
	}

	void LightHandler::newFrame()
	{
		m_shadowMapPool.m_numActive = 0;
		m_shadowMapCubePool.m_numActive = 0;
	}

	void LightHandler::trySetShadowMapData(glm::mat4 viewProjMatrix, uint32_t* pOutShadowMapIdx)
	{
		if (m_shadowMapPool.m_numActive >= m_shadowMapPool.getMaxSize())
		{
			*pOutShadowMapIdx = INVALID_UINT32;
			return;
		}

		ShadowMap* pShadowMap = nullptr;
		if (m_shadowMapPool.m_numActive >= m_shadowMapPool.getSize())
		{
			pShadowMap = &createShadowMap(SHADOW_MAPS_WIDTH, SHADOW_MAPS_HEIGHT);
		}
		else
		{
			pShadowMap = &m_shadowMapPool[m_shadowMapPool.m_numActive];
		}

		pShadowMap->viewProjMatrix = viewProjMatrix;
		*pOutShadowMapIdx = m_shadowMapPool.m_numActive++;
	}

	void LightHandler::trySetShadowMapCubeData(glm::mat4* pViewProjMatrices, glm::vec3 lightPos, uint32_t* pOutShadowMapIdx)
	{
		if (m_shadowMapCubePool.m_numActive >= m_shadowMapCubePool.getMaxSize())
		{
			*pOutShadowMapIdx = INVALID_UINT32;
			return;
		}

		ShadowMapCube* pShadowMapCube = nullptr;
		if (m_shadowMapCubePool.m_numActive >= m_shadowMapCubePool.getSize())
		{
			pShadowMapCube = &createShadowMapCube(SHADOW_MAPS_WIDTH, SHADOW_MAPS_HEIGHT);
		}
		else
		{
			pShadowMapCube = &m_shadowMapCubePool[m_shadowMapCubePool.m_numActive];
		}

		pShadowMapCube->lightPos = lightPos;
		memcpy(pShadowMapCube->viewProjMatrices, pViewProjMatrices, sizeof(pShadowMapCube->viewProjMatrices));

		*pOutShadowMapIdx = m_shadowMapCubePool.m_numActive++;
	}

	ShadowMap& LightHandler::createShadowMap(uint32_t width, uint32_t height)
	{
		ShadowMap& shadowMap = m_shadowMapPool.emplace();

		shadowMap.textureAllocation = m_pGpuResourceManager->createTexture(width, height, 1, 1, DXGI_FORMAT_R32_TYPELESS,
			OKAY_TEXTURE_FLAG_DEPTH | OKAY_TEXTURE_FLAG_SHADER_READ, nullptr);

		ID3D12Resource* pDXTexture = m_pGpuResourceManager->getDXResource(shadowMap.textureAllocation.resourceHandle);


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

		shadowMap.srvHandle = m_pDescriptorHeapStore->allocateDescriptors(m_shadowMapDHH, m_shadowMapPool.getSize() - 1, &shadowMapDescriptorDesc, 1).gpuHandle;


		shadowMapDescriptorDesc.type = OKAY_DESCRIPTOR_TYPE_DSV;
		shadowMapDescriptorDesc.dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		shadowMapDescriptorDesc.dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		shadowMapDescriptorDesc.dsvDesc.Texture2D.MipSlice = 0;
		shadowMapDescriptorDesc.dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		shadowMap.dsvHandle = m_pDescriptorHeapStore->allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &shadowMapDescriptorDesc, 1).cpuHandle;

		m_pCommandContext->transitionResource(pDXTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		return shadowMap;
	}

	ShadowMapCube& LightHandler::createShadowMapCube(uint32_t width, uint32_t height)
	{
		ShadowMapCube& shadowMap = m_shadowMapCubePool.emplace();

		shadowMap.textureAllocation = m_pGpuResourceManager->createTexture(width, height, 1, 6, DXGI_FORMAT_R32_TYPELESS,
			OKAY_TEXTURE_FLAG_DEPTH | OKAY_TEXTURE_FLAG_SHADER_READ, nullptr);

		ID3D12Resource* pDXTexture = m_pGpuResourceManager->getDXResource(shadowMap.textureAllocation.resourceHandle);


		DescriptorDesc shadowMapSRVDesc = {};
		shadowMapSRVDesc.type = OKAY_DESCRIPTOR_TYPE_SRV;
		shadowMapSRVDesc.pDXResource = pDXTexture;
		shadowMapSRVDesc.nullDesc = false;
		shadowMapSRVDesc.srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		shadowMapSRVDesc.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		shadowMapSRVDesc.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		shadowMapSRVDesc.srvDesc.TextureCube.MipLevels = 1;
		shadowMapSRVDesc.srvDesc.TextureCube.MostDetailedMip = 0;
		shadowMapSRVDesc.srvDesc.TextureCube.ResourceMinLODClamp = 0;

		shadowMap.srvHandle = m_pDescriptorHeapStore->allocateDescriptors(m_shadowMapDHH, MAX_SHADOW_MAPS + m_shadowMapCubePool.getSize() - 1, &shadowMapSRVDesc, 1).gpuHandle;


		DescriptorDesc shadowMapDSVDesc = {};
		shadowMapDSVDesc.type = OKAY_DESCRIPTOR_TYPE_DSV;
		shadowMapDSVDesc.pDXResource = pDXTexture;
		shadowMapDSVDesc.nullDesc = false;
		shadowMapDSVDesc.dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		shadowMapDSVDesc.dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		shadowMapDSVDesc.dsvDesc.Texture2DArray.MipSlice = 0;
		shadowMapDSVDesc.dsvDesc.Texture2DArray.ArraySize = 6;
		shadowMapDSVDesc.dsvDesc.Texture2DArray.FirstArraySlice = 0;
		shadowMapDSVDesc.dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		shadowMap.dsvHandle = m_pDescriptorHeapStore->allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &shadowMapDSVDesc, 1).cpuHandle;

		m_pCommandContext->transitionResource(pDXTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		return shadowMap;
	}

	void LightHandler::createRenderPasses()
	{
		ID3DBlob* shaderBlobs[5] = {};
		uint32_t nextBlobIdx = 0;


		std::vector<D3D12_ROOT_PARAMETER> rootParams;

		rootParams.emplace_back(createRootParamCBV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Light Data
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 0, 0)); // Verticies SRV
		rootParams.emplace_back(createRootParamSRV(D3D12_SHADER_VISIBILITY_ALL, 1, 0)); // Object datas (GPUObjcetData)

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = (uint32_t)rootParams.size();
		rootSignatureDesc.pParameters = rootParams.data();

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = createDefaultGraphicsPipelineStateDesc();
		pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
		pipelineDesc.NumRenderTargets = 0;
		pipelineDesc.VS = compileShader(SHADER_PATH / "ShadowVS.hlsl", "vs_5_1", &shaderBlobs[nextBlobIdx++]);

		m_shadowPass.initialize(m_pDevice, pipelineDesc, rootSignatureDesc);

		D3D12_VIEWPORT shadowViewport = createViewport((float)SHADOW_MAPS_WIDTH, (float)SHADOW_MAPS_HEIGHT);
		D3D12_RECT shadowScissorRect = createRect(SHADOW_MAPS_WIDTH, SHADOW_MAPS_HEIGHT);
		m_shadowPass.updateProperties(shadowViewport, shadowScissorRect, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		nextBlobIdx = 0;
		for (ID3DBlob*& pBlob : shaderBlobs)
		{
			D3D12_RELEASE(pBlob);
		}


		// Point Light Shadow Pass

		pipelineDesc.VS = compileShader(SHADER_PATH / "ShadowCubeVS.hlsl", "vs_5_1", &shaderBlobs[nextBlobIdx++]);
		pipelineDesc.GS = compileShader(SHADER_PATH / "ShadowCubeGS.hlsl", "gs_5_1", &shaderBlobs[nextBlobIdx++]);
		pipelineDesc.PS = compileShader(SHADER_PATH / "ShadowCubePS.hlsl", "ps_5_1", &shaderBlobs[nextBlobIdx++]);

		m_shadowPassPointLights.initialize(m_pDevice, pipelineDesc, rootSignatureDesc);
		m_shadowPassPointLights.updateProperties(shadowViewport, shadowScissorRect, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		nextBlobIdx = 0;
		for (ID3DBlob*& pBlob : shaderBlobs)
		{
			D3D12_RELEASE(pBlob);
		}
	}
}

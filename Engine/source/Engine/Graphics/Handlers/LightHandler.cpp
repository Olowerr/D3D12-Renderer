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

	void LightHandler::initiate(ID3D12Device* pDevice, GPUResourceManager& gpuResourceManager, CommandContext& commandContext, const std::vector<DXMesh>& dxMeshes)
	{
		m_pDevice = pDevice;
		m_pGpuResourceManager = &gpuResourceManager;
		m_pCommandContext = &commandContext;
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
	
	D3D12_GPU_VIRTUAL_ADDRESS LightHandler::writePointLightGPUData(const Scene& scene, RingBuffer& ringBuffer, uint32_t* pOutNumPointLights)
	{
		D3D12_GPU_VIRTUAL_ADDRESS gpuPointLightsGVA = ringBuffer.getCurrentGPUAddress();

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
		*pOutNumPointLights = (uint32_t)pointLightView.size_hint();

		for (entt::entity entity : pointLightView)
		{
			GPUPointLight* pGpuPointLight = (GPUPointLight*)ringBuffer.getMappedPtr();
			ringBuffer.offsetMappedPtr(sizeof(GPUPointLight));

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

			trySetShadowMapData(m_shadowMapCubePool, true, viewProjMatrices, transform.position, &pGpuPointLight->shadowMapIdx);
		}

		ringBuffer.alignOffset();
		return gpuPointLightsGVA;
	}
	
	D3D12_GPU_VIRTUAL_ADDRESS LightHandler::writeDirLightGPUData(const Scene& scene, RingBuffer& ringBuffer, uint32_t* pOutNumDirLights)
	{
		D3D12_GPU_VIRTUAL_ADDRESS gpuDirLightsGVA = ringBuffer.getCurrentGPUAddress();

		auto dirLightView = scene.getRegistry().view<DirectionalLight, Transform>();
		*pOutNumDirLights = (uint32_t)dirLightView.size_hint();

		const Transform& camTransform = scene.getActiveCamera().getComponent<Transform>();

		for (entt::entity entity : dirLightView)
		{
			GPUDirectionalLight* pGpuDirLight = (GPUDirectionalLight*)ringBuffer.getMappedPtr();
			ringBuffer.offsetMappedPtr(sizeof(GPUDirectionalLight));

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

			// light pos not used for directional lights
			trySetShadowMapData(m_shadowMapPool, false, &pGpuDirLight->viewProjMatrix, glm::vec3(0.f), &pGpuDirLight->shadowMapIdx);

			/*
				Shadow map improvement techniques
				https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps

				Frustum Culling (extracting frustum planes)
				https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling

				// EXtracting frustum planes from matrix
				https://www.reddit.com/r/gamedev/comments/xj47t/does_glm_support_frustum_plane_extraction/
			*/
		}
		
		ringBuffer.alignOffset();
		return gpuDirLightsGVA;
	}
	
	D3D12_GPU_VIRTUAL_ADDRESS LightHandler::writeSpotLightGPUData(const Scene& scene, RingBuffer& ringBuffer, uint32_t* pOutNumSpotLights)
	{
		D3D12_GPU_VIRTUAL_ADDRESS gpuSpotLightsGVA = ringBuffer.getCurrentGPUAddress();

		auto spotLightView = scene.getRegistry().view<SpotLight, Transform>();
		*pOutNumSpotLights = (uint32_t)spotLightView.size_hint();

		for (entt::entity entity : spotLightView)
		{
			GPUSpotLight* pGpuSpotLight = (GPUSpotLight*)ringBuffer.getMappedPtr();
			ringBuffer.offsetMappedPtr(sizeof(GPUSpotLight));

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

			// light pos not used for spot lights
			trySetShadowMapData(m_shadowMapPool, false, &pGpuSpotLight->viewProjMatrix, glm::vec3(0.f), &pGpuSpotLight->shadowMapIdx);
		}

		ringBuffer.alignOffset();
		return gpuSpotLightsGVA;
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

	void LightHandler::drawDepthMap_Internal(const std::vector<DrawGroup>& drawGroups, uint32_t numActiveDrawGroups, const ShadowMap& shadowMap, uint32_t shadowBarrierIdx)
	{
		ID3D12GraphicsCommandList* pCommandList = m_pCommandContext->getCommandList();

		for (uint32_t i = 0; i < numActiveDrawGroups; i++)
		{
			const DrawGroup& drawGroup = drawGroups[i];

			const DXMesh& dxMesh = (*m_pDxMeshes)[drawGroup.dxMeshId];

			pCommandList->SetGraphicsRootShaderResourceView(1, dxMesh.gpuVerticiesGVA);
			pCommandList->IASetIndexBuffer(&dxMesh.indiciesView);

			pCommandList->SetGraphicsRootShaderResourceView(2, drawGroup.objectDatasVA);

			pCommandList->DrawIndexedInstanced(dxMesh.numIndicies, (uint32_t)drawGroup.entities.size(), 0, 0, 0);
		}

		s_shadowMapBarriers[shadowBarrierIdx].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		s_shadowMapBarriers[shadowBarrierIdx].Transition.pResource = m_pGpuResourceManager->getDXResource(shadowMap.textureAllocation.resourceHandle);
		s_shadowMapBarriers[shadowBarrierIdx].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		s_shadowMapBarriers[shadowBarrierIdx].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		s_shadowMapBarriers[shadowBarrierIdx].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		s_shadowMapBarriers[shadowBarrierIdx].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	}

	void LightHandler::drawDepthMaps(const std::vector<DrawGroup>& drawGroups, uint32_t numActiveDrawGroups, RingBuffer& ringBuffer)
	{
		preDepthMapRender();

		ID3D12GraphicsCommandList* pCommandList = m_pCommandContext->getCommandList();
		m_shadowPass.bindBase(pCommandList);

		for (uint32_t i = 0; i < m_shadowMapPool.m_numActive; i++)
		{
			const ShadowMap& shadowMap = m_shadowMapPool[i];

			D3D12_GPU_VIRTUAL_ADDRESS lightCamBuffer = ringBuffer.allocateMapped(shadowMap.viewProjMatrices, sizeof(glm::mat4));
			pCommandList->SetGraphicsRootConstantBufferView(0, lightCamBuffer);

			m_shadowPass.bindRTVs(pCommandList, 0, nullptr, &shadowMap.dsvHandle, 1);
			drawDepthMap_Internal(drawGroups, numActiveDrawGroups, shadowMap, i);
		}


		m_shadowPassPointLights.bindBase(pCommandList);

		for (uint32_t i = 0; i < m_shadowMapCubePool.m_numActive; i++)
		{
			const ShadowMap& shadowMapCube = m_shadowMapCubePool[i];

			D3D12_GPU_VIRTUAL_ADDRESS lightCamBuffer = ringBuffer.getCurrentGPUAddress();

			GPUShadowMapCubeData* pShadowMapData = (GPUShadowMapCubeData*)ringBuffer.getMappedPtr();
			memcpy(pShadowMapData->matrices, shadowMapCube.viewProjMatrices, sizeof(glm::mat4) * 6);
			pShadowMapData->lightPos = shadowMapCube.lightPos;
			pShadowMapData->farPlane = 3000.f;

			pCommandList->SetGraphicsRootConstantBufferView(0, lightCamBuffer);

			m_shadowPassPointLights.bindRTVs(pCommandList, 0, nullptr, &shadowMapCube.dsvHandle, 6);
			drawDepthMap_Internal(drawGroups, numActiveDrawGroups, shadowMapCube, i + m_shadowMapPool.m_numActive);
		}

		pCommandList->ResourceBarrier(m_shadowMapPool.m_numActive + m_shadowMapCubePool.m_numActive, s_shadowMapBarriers);
	}

	void LightHandler::newFrame()
	{
		m_shadowMapPool.m_numActive = 0;
		m_shadowMapCubePool.m_numActive = 0;
	}

	template<uint32_t containerSize>
	void LightHandler::trySetShadowMapData(StaticContainer<ShadowMap, containerSize>& container, bool isCubeMap, glm::mat4* pViewProjMatrices, glm::vec3 lightPos, uint32_t* pOutShadowMapIdx)
	{
		if (container.m_numActive >= container.getMaxSize())
		{
			*pOutShadowMapIdx = INVALID_UINT32;
			return;
		}

		ShadowMap* pShadowMap = nullptr;
		if (container.m_numActive >= container.getSize())
		{
			pShadowMap = &createShadowMap(container, isCubeMap, SHADOW_MAPS_WIDTH, SHADOW_MAPS_HEIGHT);
		}
		else
		{
			pShadowMap = &container[container.m_numActive];
		}

		pShadowMap->lightPos = lightPos;
		memcpy(pShadowMap->viewProjMatrices, pViewProjMatrices, sizeof(pShadowMap->viewProjMatrices));

		*pOutShadowMapIdx = container.m_numActive++;
	}

	template<uint32_t containerSize>
	ShadowMap& LightHandler::createShadowMap(StaticContainer<ShadowMap, containerSize>& container, bool isCubeMap, uint32_t width, uint32_t height)
	{
		ShadowMap& shadowMap = container.emplace();

		uint32_t arraySize = isCubeMap ? 6 : 1;
		shadowMap.textureAllocation = m_pGpuResourceManager->createTexture(width, height, 1, arraySize, DXGI_FORMAT_R32_TYPELESS,
			OKAY_TEXTURE_FLAG_DEPTH | OKAY_TEXTURE_FLAG_SHADER_READ, nullptr);

		ID3D12Resource* pDXTexture = m_pGpuResourceManager->getDXResource(shadowMap.textureAllocation.resourceHandle);


		DescriptorDesc srvDesc = {};
		srvDesc.type = OKAY_DESCRIPTOR_TYPE_SRV;
		srvDesc.pDXResource = pDXTexture;
		srvDesc.nullDesc = false;
		srvDesc.srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		DescriptorDesc dsvDesc = srvDesc;
		dsvDesc.type = OKAY_DESCRIPTOR_TYPE_DSV;
		dsvDesc.dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		if (isCubeMap)
		{
			srvDesc.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.srvDesc.TextureCube.MipLevels = 1;
			srvDesc.srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.srvDesc.TextureCube.ResourceMinLODClamp = 0;

			dsvDesc.dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			dsvDesc.dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.dsvDesc.Texture2DArray.ArraySize = 6;
			dsvDesc.dsvDesc.Texture2DArray.FirstArraySlice = 0;
		}
		else
		{
			srvDesc.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.srvDesc.Texture2D.MipLevels = 1;
			srvDesc.srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.srvDesc.Texture2D.PlaneSlice = 0;
			srvDesc.srvDesc.Texture2D.ResourceMinLODClamp = 0.f;

			dsvDesc.dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.dsvDesc.Texture2D.MipSlice = 0;
		}

		uint32_t srvDescriptorOffset = isCubeMap ? MAX_SHADOW_MAPS : 0;
		shadowMap.srvHandle = m_pDescriptorHeapStore->allocateDescriptors(m_shadowMapDHH, srvDescriptorOffset + container.getSize() - 1, &srvDesc, 1).gpuHandle;

		shadowMap.dsvHandle = m_pDescriptorHeapStore->allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, &dsvDesc, 1).cpuHandle;

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

#pragma once

#include "Engine/OkayD3D12.h"
#include "Engine/Graphics/DescriptorHeapStore.h"
#include "Engine/Graphics/RenderPass.h"
#include "Engine/Misc/StaticContainer.h"
#include "Engine/Scene/Scene.h"

namespace Okay
{
	class LightHandler
	{
	public:
		static const uint32_t SHADOW_MAPS_WIDTH = 2048;
		static const uint32_t SHADOW_MAPS_HEIGHT = 2048;
		static const uint32_t MAX_SHADOW_MAPS = 32;

		static const uint32_t MAX_POINT_SHADOW_CUBES = 8;

		static const uint32_t DIR_LIGHT_RANGE = 10000;
		static const uint32_t DIR_LIGHT_SHADOW_WORLD_WIDTH = 4000;
		static const uint32_t DIR_LIGHT_SHADOW_WORLD_HEIGHT = 4000;

	public:
		LightHandler() = default;
		virtual ~LightHandler() = default;

		void initiate(ID3D12Device* pDevice, GPUResourceManager& gpuResourceManager, CommandContext& commandContext, RingBuffer& ringBuffer, const std::vector<DXMesh>& dxMeshes);
		void shutdown();

		void setShadowMapDescritprHeap(DescriptorHeapStore& store, DescriptorHeapHandle shadowMapHandle, uint32_t offset);

		uint64_t writePointLightGPUData(const Scene& scene, uint8_t* pWriteLocation, uint32_t* pOutNumPointLights);
		uint64_t writeDirLightGPUData(const Scene& scene, uint8_t* pWriteLocation, uint32_t* pOutNumDirLights);
		uint64_t writeSpotLightGPUData(const Scene& scene, uint8_t* pWriteLocation, uint32_t* pOutNumSpotLights);

		void drawDepthMaps(const std::vector<DrawGroup>& drawGroups, uint32_t numActiveDrawGroups);
		void newFrame();

	private:
		void preDepthMapRender();
		void drawDepthMap_Internal(const std::vector<DrawGroup>& drawGroups, uint32_t numActiveDrawGroups, uint8_t* pMappedRingBuffer, const ShadowMap& shadowMap, uint32_t shadowBarrierIdx);

		template<uint32_t containerSize>
		void trySetShadowMapData(StaticContainer<ShadowMap, containerSize>& container, bool isCubeMap, glm::mat4* pViewProjMatrices, glm::vec3 lightPos, uint32_t* pOutShadowMapIdx);

		template<uint32_t containerSize>
		ShadowMap& createShadowMap(StaticContainer<ShadowMap, containerSize>& container, bool isCubeMap, uint32_t width, uint32_t height);

		void createRenderPasses();

	private:
		ID3D12Device* m_pDevice = nullptr;

		GPUResourceManager* m_pGpuResourceManager = nullptr;
		CommandContext* m_pCommandContext = nullptr;
		RingBuffer* m_pRingBuffer = nullptr;
		const std::vector<DXMesh>* m_pDxMeshes;

		DescriptorHeapStore* m_pDescriptorHeapStore = nullptr;
		DescriptorHeapHandle m_shadowMapDHH = INVALID_DHH;
		uint32_t m_shadowDescriptorsOffset = INVALID_UINT32;

	private:
		RenderPass m_shadowPass;
		RenderPass m_shadowPassPointLights;

		StaticContainer<ShadowMap, MAX_SHADOW_MAPS> m_shadowMapPool;
		StaticContainer<ShadowMap, MAX_POINT_SHADOW_CUBES> m_shadowMapCubePool;

	};
}

#pragma once

#include "Engine/OkayD3D12.h"
#include "Engine/Graphics/DescriptorHeapStore.h"
#include "Engine/Graphics/RenderPass.h"
#include "Engine/Misc/ActiveVector.h"
#include "Engine/Scene/Scene.h"

namespace Okay
{
	class LightHandler
	{
	public:
		static const uint32_t SHADOW_MAPS_WIDTH = 2048;
		static const uint32_t SHADOW_MAPS_HEIGHT = 2048;

		// TODO: Fix lightMaps for frameInFlight, create copies or take from pool? :spinthink:
		static const uint32_t MAX_SHADOW_MAPS = 32;
		static const uint32_t MAX_POINT_SHADOW_CUBES = 8;

		static const uint32_t DIR_LIGHT_RANGE = 10000;
		static const uint32_t DIR_LIGHT_SHADOW_WORLD_WIDTH = 4000;
		static const uint32_t DIR_LIGHT_SHADOW_WORLD_HEIGHT = 4000;

		struct FrameResources
		{
			ActiveVector<ShadowMap> shadowMapPool;
			ActiveVector<ShadowMap> shadowMapCubePool;

			uint32_t shadowMapDescriptorsOffset = INVALID_UINT32;

			D3D12_RESOURCE_BARRIER shadowMapBarriers[MAX_SHADOW_MAPS + MAX_POINT_SHADOW_CUBES] = {};
		};

	public:
		LightHandler() = default;
		virtual ~LightHandler() = default;

		void initiate(ID3D12Device* pDevice, uint32_t maxFramesInFlight, GPUResourceManager& gpuResourceManager, const std::vector<DXMesh>& dxMeshes, DescriptorHeapStore& descHeapStore);
		void shutdown();

		void newFrame();

		void drawDepthMaps(CommandContext& commandContext, RingBuffer& ringBuffer, const std::vector<DrawGroup>& drawGroups, uint32_t numActiveDrawGroups);
		D3D12_CPU_DESCRIPTOR_HANDLE getFrameShadowMapSRVCPUHandle();

		D3D12_GPU_VIRTUAL_ADDRESS writePointLightGPUData(RingBuffer& ringBuffer, CommandContext& commandContext, const Scene& scene, uint32_t* pOutNumPointLights);
		D3D12_GPU_VIRTUAL_ADDRESS writeDirLightGPUData(RingBuffer& ringBuffer, CommandContext& commandContext, const Scene& scene, uint32_t* pOutNumDirLights);
		D3D12_GPU_VIRTUAL_ADDRESS writeSpotLightGPUData(RingBuffer& ringBuffer, CommandContext& commandContext, const Scene& scene, uint32_t* pOutNumSpotLights);


	private:
		void preDepthMapRender(CommandContext& commandContext);
		void drawDepthMap_Internal(CommandContext& commandContext, const std::vector<DrawGroup>& drawGroups, uint32_t numActiveDrawGroups, const ShadowMap& shadowMap, uint32_t shadowBarrierIdx);

		void trySetShadowMapData(CommandContext& commandContext, ActiveVector<ShadowMap>& shadowMaps, bool isCubeMap, glm::mat4* pViewProjMatrices, glm::vec3 lightPos, uint32_t* pOutShadowMapIdx);
		ShadowMap& createShadowMap(CommandContext& commandContext, ActiveVector<ShadowMap>& shadowMaps, bool isCubeMap, uint32_t width, uint32_t height);

		void createRenderPasses();

	private:
		ID3D12Device* m_pDevice = nullptr;

		GPUResourceManager* m_pGpuResourceManager = nullptr;
		const std::vector<DXMesh>* m_pDxMeshes;

		uint32_t m_srvIncrementSize = INVALID_UINT32;

		DescriptorHeapStore* m_pDescriptorHeapStore = nullptr;
		DescriptorHeapHandle m_shadowMapsDHH = INVALID_DHH;

	private:
		RenderPass m_shadowPass;
		RenderPass m_shadowPassPointLights;

		uint32_t m_maxFramesInFlight = INVALID_UINT32;
		uint32_t m_currentFrame = INVALID_UINT32;
		std::vector<FrameResources> m_frames;

	};
}

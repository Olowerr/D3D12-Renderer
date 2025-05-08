#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Scene/Scene.h"
#include "RenderPass.h"
#include "RingBuffer.h"

namespace Okay
{
	class ResourceManager;
	class Mesh;
	class Texture;

	struct DXMesh
	{
		DXMesh() = default;

		D3D12_GPU_VIRTUAL_ADDRESS gpuVerticiesGVA = {};
		D3D12_INDEX_BUFFER_VIEW indiciesView = {};
		uint32_t numIndicies = INVALID_UINT32;
	};

	struct DrawGroup
	{
		DrawGroup() = default;

		uint32_t dxMeshId = INVALID_UINT32;
		std::vector<entt::entity> entities;

		D3D12_GPU_VIRTUAL_ADDRESS objectDatasVA = INVALID_UINT64;
	};

	struct ShadowMap
	{
		Allocation textureAllocation = {};
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = {};

		glm::mat4 viewProjMatrix = glm::mat4(1.f);
	};

	class Renderer
	{
	public:
		static const uint8_t NUM_BACKBUFFERS = 2;
		static const uint8_t MAX_MIP_LEVELS = 16;

		static const uint32_t SHADOW_MAPS_WIDTH = 2048;
		static const uint32_t SHADOW_MAPS_HEIGHT = 2048;
		static const uint32_t MAX_SHADOW_MAPS = 32;

		static const uint32_t DIR_LIGHT_RANGE = 10000;
		static const uint32_t DIR_LIGHT_SHADOW_WORLD_WIDTH = 4000;
		static const uint32_t DIR_LIGHT_SHADOW_WORLD_HEIGHT = 4000;

	public:
		Renderer() = default;
		virtual ~Renderer() = default;

		void initialize(const Window& window);
		void shutdown();

		void render(const Scene& scene);

		void preProcessResources(const ResourceManager& resourceManager);

	private:
		void updateBuffers(const Scene& scene);
		void preRender(D3D12_CPU_DESCRIPTOR_HANDLE* pOutCurrentBB);
		void renderScene(const Scene& scene, D3D12_CPU_DESCRIPTOR_HANDLE currentMainRtv);
		void postRender();

		void assignObjectDrawGroups(const Scene& scene);

		void createDevice(IDXGIFactory* pFactory);

		void createSwapChain(IDXGIFactory* pFactory, const Window& window);
		void fetchBackBuffersAndDSV();

		void createRenderPasses();

		ShadowMap& createShadowMap(uint32_t width, uint32_t height);
		void trySetShadowMapData(glm::mat4 viewProjMatrix, uint32_t* pOutShadowMapIdx);

		void preProcessMeshes(const std::vector<Mesh>& meshes);
		void preProcessTextures(const std::vector<Texture>& textures);

		void enableDebugLayer();
		void enableGPUBasedValidation();

	private: // Main
		ID3D12Device* m_pDevice = nullptr;
		IDXGISwapChain1* m_pSwapChain = nullptr;

		ID3D12Resource* m_backBuffers[NUM_BACKBUFFERS] = {};
		uint8_t m_currentBackBuffer = NUM_BACKBUFFERS - 1;
		D3D12_CPU_DESCRIPTOR_HANDLE m_rtvBackBufferCPUHandle;
		D3D12_CPU_DESCRIPTOR_HANDLE m_mainDsvCpuHandle;

		D3D12_VIEWPORT m_viewport = {};
		D3D12_RECT m_scissorRect = {};

	private: // Abstractions + increment size
		CommandContext m_commandContext;
		GPUResourceManager m_gpuResourceManager;
		DescriptorHeapStore m_descriptorHeapStore;
		RingBuffer m_ringBuffer;

		uint32_t m_rtvDescriptorSize = INVALID_UINT32;

	private: // Draw
		D3D12_GPU_VIRTUAL_ADDRESS m_renderDataGVA = INVALID_UINT64;
		DescriptorHeapHandle m_materialTexturesDHH = INVALID_DHH;
		RenderPass m_mainRenderPass;

		std::vector<DXMesh> m_dxMeshes;

		// Keep track manually because we don't wanna call clear() cuz it deallocates the std::vectors inside the DrawGroups
		uint32_t m_activeDrawGroups = INVALID_UINT32;
		std::vector<DrawGroup> m_drawGroups;

	private: // Shadows
		RenderPass m_shadowPass;

		uint32_t m_numActiveShadowMaps = INVALID_UINT32;
		std::vector<ShadowMap> m_shadowMapPool;

	private: // Lights
		D3D12_GPU_VIRTUAL_ADDRESS m_pointLightsGVA = INVALID_UINT64;
		D3D12_GPU_VIRTUAL_ADDRESS m_directionalLightsGVA = INVALID_UINT64;
		D3D12_GPU_VIRTUAL_ADDRESS m_spotLightsGVA = INVALID_UINT64;

	private: // Misc
		ID3D12DescriptorHeap* m_pImguiDescriptorHeap = nullptr;
	};
}

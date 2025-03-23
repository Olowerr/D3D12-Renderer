#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Scene/Scene.h"
#include "RenderPass.h"

namespace Okay
{
	class ResourceManager;

	struct DXMesh
	{
		DXMesh() = default;

		D3D12_GPU_VIRTUAL_ADDRESS gpuVerticies = {};
		D3D12_INDEX_BUFFER_VIEW indiciesView = {};
		uint32_t numIndicies = INVALID_UINT32;
	};

	struct DrawGroup
	{
		DrawGroup() = default;

		uint32_t dxMeshId = INVALID_UINT32;
		std::vector<entt::entity> entities;
	};

	class Renderer
	{
	public:
		static const uint32_t NUM_BACKBUFFERS = 2;

	public:
		Renderer() = default;
		virtual ~Renderer() = default;

		void initialize(const Window& window);
		void shutdown();

		void render(const Scene& scene);

		void preProcessResources(const ResourceManager& resourceManager);

	private:
		void updateBuffers(const Scene& scene);
		void preRender();
		void renderScene(const Scene& scene);
		void postRender();

		void assignObjectDrawGroups(const Scene& scene);

		void createDevice(IDXGIFactory* pFactory);

		void createSwapChain(IDXGIFactory* pFactory, const Window& window);
		void fetchBackBuffersAndDSV();

		void createMainRenderPass();

		void enableDebugLayer();
		void enableGPUBasedValidation();

	private:
		ID3D12Device* m_pDevice = nullptr;
		IDXGISwapChain1* m_pSwapChain = nullptr;

		ID3D12Resource* m_backBuffers[NUM_BACKBUFFERS] = {};
		uint8_t m_currentBackBuffer = NUM_BACKBUFFERS - 1;
		Descriptor m_rtvFirstDescriptor;
		Descriptor m_dsvDescriptor;

		D3D12_VIEWPORT m_viewport = {};
		D3D12_RECT m_scissorRect = {};

	private:
		CommandContext m_commandContext;
		GPUResourceManager m_gpuResourceManager;
		DescriptorHeapStore m_descriptorHeapStore;

		uint32_t m_cbvSrvUavDescriptorSize = INVALID_UINT32;
		uint32_t m_rtvDescriptorSize = INVALID_UINT32;
		uint32_t m_dsvDescriptorSize = INVALID_UINT32;

	private:
		Allocation m_renderData;
		Allocation m_instancedObjectData;
		RenderPass m_mainRenderPass;

		std::vector<DXMesh> m_dxMeshes;

		// Keep track manually because we don't wanna call clear() cuz it deallocates the std::vectors inside the DrawGroups
		uint32_t m_activeDrawGroups = INVALID_UINT32;
		std::vector<DrawGroup> m_drawGroups;
	};
}

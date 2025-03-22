#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Scene/Scene.h"
#include "RenderPass.h"

namespace Okay
{
	inline const std::filesystem::path SHADER_PATH = std::filesystem::path("..") / "Engine" / "resources" / "Shaders";

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

	private:
		void updateBuffers(const Scene& scene);
		void preRender();
		void renderScene(const Scene& scene);
		void postRender();

		void enableDebugLayer();
		void enableGPUBasedValidation();

		void createDevice(IDXGIFactory* pFactory);

		void createSwapChain(IDXGIFactory* pFactory, const Window& window);
		void fetchBackBuffersAndDSV();

	private:
		ID3D12Device* m_pDevice = nullptr;
		IDXGISwapChain1* m_pSwapChain = nullptr;

		ID3D12Resource* m_backBuffers[NUM_BACKBUFFERS] = {};
		uint8_t m_currentBackBuffer = NUM_BACKBUFFERS - 1;
		DescriptorHandle m_rtvFirstDescriptor = INVALID_UINT32;
		DescriptorHandle m_dsvDescriptor = INVALID_UINT32;

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

	private: // temp
		RenderPass m_mainRenderPass;
		Allocation m_triangleColour;
		Allocation m_vertexBuffer;
		Allocation m_indexBuffer;
		void createMainRenderPass();
	};
}

#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Scene/Scene.h"
#include "RenderPass.h"
#include "RingBuffer.h"
#include "Engine/Misc/StaticContainer.h"
#include "Handlers/LightHandler.h"

namespace Okay
{
	class ResourceManager;
	class Mesh;
	class Texture;

	class Renderer
	{
	public:
		static const uint8_t NUM_BACKBUFFERS = 2;
		static const uint8_t MAX_MIP_LEVELS = 16;

	public:
		Renderer() = default;
		virtual ~Renderer() = default;

		void initialize(const Window& window);
		void shutdown();

		void render(const Scene& scene);

		void preProcessResources(const ResourceManager& resourceManager);

	private:
		void drawDrawGroups(ID3D12GraphicsCommandList* pCommandList);

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

		LightHandler m_lightHandler;

		uint32_t m_rtvIncrementSize = INVALID_UINT32;
		uint32_t m_dsvIncrementSize = INVALID_UINT32;

	private: // Draw
		D3D12_GPU_VIRTUAL_ADDRESS m_renderDataGVA = INVALID_UINT64;
		DescriptorHeapHandle m_materialTexturesDHH = INVALID_DHH;
		RenderPass m_mainRenderPass;

		std::vector<DXMesh> m_dxMeshes;

		// Keep track manually because we don't wanna call clear() cuz it deallocates the std::vectors inside the DrawGroups
		uint32_t m_activeDrawGroups = INVALID_UINT32;
		std::vector<DrawGroup> m_drawGroups;

		D3D12_GPU_VIRTUAL_ADDRESS m_pointLightsGVA = INVALID_UINT64;
		D3D12_GPU_VIRTUAL_ADDRESS m_directionalLightsGVA = INVALID_UINT64;
		D3D12_GPU_VIRTUAL_ADDRESS m_spotLightsGVA = INVALID_UINT64;

	private: // Misc
		ID3D12DescriptorHeap* m_pImguiDescriptorHeap = nullptr;
	};
}

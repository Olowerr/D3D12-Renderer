#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Scene/Scene.h"
#include "RenderPass.h"
#include "RingBuffer.h"
#include "Handlers/LightHandler.h"

#include <array>

namespace Okay
{
	class ResourceManager;
	class Mesh;
	class Texture;

	class Renderer
	{
	public:
		static const uint8_t MAX_FRAMES_IN_FLIGHT = 3;
		static const uint8_t MAX_MIP_LEVELS = 16;

		struct FrameResources
		{
			CommandContext commandContext;

			// Draw
			ID3D12Resource* backBuffer = nullptr;
			D3D12_GPU_VIRTUAL_ADDRESS renderDataGVA = INVALID_UINT64;
			D3D12_CPU_DESCRIPTOR_HANDLE dsvCpuHandle;

			// Technically doesn't need 1 per frame but it's okay for now :3
			RingBuffer ringBuffer;

			// Use ActiveVector cuz we don't wanna call the desctructors of DrawGroup cuz they deallocate the vectors inside them
			ActiveVector<DrawGroup> drawGroups;

			D3D12_GPU_VIRTUAL_ADDRESS pointLightsGVA = INVALID_UINT64;
			D3D12_GPU_VIRTUAL_ADDRESS directionalLightsGVA = INVALID_UINT64;
			D3D12_GPU_VIRTUAL_ADDRESS spotLightsGVA = INVALID_UINT64;
		};

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
		void createCommandQueue();

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
		ID3D12CommandQueue* m_pCommandQueue = nullptr;

		uint8_t m_currentBackBuffer = MAX_FRAMES_IN_FLIGHT - 1;
		D3D12_CPU_DESCRIPTOR_HANDLE m_rtvBackBufferCPUHandle;

		D3D12_VIEWPORT m_viewport = {};
		D3D12_RECT m_scissorRect = {};

		FrameResources m_frames[MAX_FRAMES_IN_FLIGHT] = {};

	private: // Abstractions + increment size
		GPUResourceManager m_gpuResourceManager;
		DescriptorHeapStore m_descriptorHeapStore;
		LightHandler m_lightHandler;

		uint32_t m_rtvIncrementSize = INVALID_UINT32;
		uint32_t m_dsvIncrementSize = INVALID_UINT32;

	private: // Draw
		DescriptorHeapHandle m_materialTexturesDHH = INVALID_DHH;
		RenderPass m_mainRenderPass;

		std::vector<DXMesh> m_dxMeshes;

	private: // Misc
		ID3D12DescriptorHeap* m_pImguiDescriptorHeap = nullptr;
	};
}

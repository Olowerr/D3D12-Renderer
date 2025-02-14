#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Scene/Scene.h"
#include "GPUResourceManager.h"

#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_2.h>

namespace Okay
{
	class Renderer
	{
	public:
		static const uint32_t NUM_BACKBUFFERS = 2;

	public:
		Renderer() = default;
		virtual ~Renderer();

		void initialize(const Window& window);
		void shutdown();

		void render(const Scene& scene);

	private:
		void createDevice(IDXGIFactory* pFactory);
		void createCommandList();

		void createSwapChain(IDXGIFactory* pFactory, const Window& window);

	private:

		ID3D12Device* m_pDevice = nullptr;

		ID3D12CommandQueue* m_pCommandQueue = nullptr;
		ID3D12CommandAllocator* m_pCommandAllocator = nullptr;
		ID3D12GraphicsCommandList* m_pCommandList = nullptr;

		IDXGISwapChain1* m_pSwapChain = nullptr;

	private:
		GPUResourceManager m_gpuResourceManager;

	};
}

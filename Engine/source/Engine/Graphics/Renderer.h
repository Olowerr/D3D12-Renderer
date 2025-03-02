#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Scene/Scene.h"
#include "GPUResourceManager.h"
#include "CommandContext.h"
#include "DescriptorHeapStore.h"

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
		virtual ~Renderer() = default;

		void initialize(const Window& window);
		void shutdown();

		void render(const Scene& scene);

	private:
		void enableDebugLayer();
		void enableGPUBasedValidation();

		void createDevice(IDXGIFactory* pFactory);
		void createSwapChain(IDXGIFactory* pFactory, const Window& window);

	private:
		ID3D12Device* m_pDevice = nullptr;
		IDXGISwapChain1* m_pSwapChain = nullptr;

	private:
		CommandContext m_commandContext;
		GPUResourceManager m_gpuResourceManager;

		DescriptorHeapStore m_cbvSrvUavDescriptorHeapStore;
		DescriptorHeapStore m_rtvDescriptorHeapStore;
		DescriptorHeapStore m_dsvDescriptorHeapStore;
	};
}

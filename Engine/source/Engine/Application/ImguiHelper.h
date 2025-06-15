#pragma once

#include <inttypes.h>

// sus?
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12DescriptorHeap;

namespace Okay
{
	class Window;
	class DescriptorHeapStore;

	void imguiInitialize(const Window& window, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, ID3D12DescriptorHeap* pImguiDescriptorHeap, uint32_t framesInFlight);
	void imguiShutdown();

	void imguiNewFrame();
	// Renderer handles ending the imgui frame

	void imguiToggleMouse(bool enabled);
}

#include "ImguiHelper.h"

#include "Window.h"
#include "Engine/Graphics/DescriptorHeapStore.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_dx12.h"

#include <bitset>

namespace Okay
{
	void imguiInitialize(const Window& window, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, ID3D12DescriptorHeap* pImguiDescriptorHeap, uint32_t framesInFlight)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		ImGui_ImplGlfw_InitForOther(window.getGLFWWindow(), true);

		ImGui_ImplDX12_InitInfo init_info = {};
		init_info.Device = pDevice;
		init_info.CommandQueue = pCommandQueue;
		init_info.NumFramesInFlight = framesInFlight;
		init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;

		init_info.SrvDescriptorHeap = pImguiDescriptorHeap;
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* pDX12InitInfo, D3D12_CPU_DESCRIPTOR_HANDLE* pOutCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGPUHandle)
			{
				pOutCPUHandle->ptr = pDX12InitInfo->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
				pOutGPUHandle->ptr = pDX12InitInfo->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr;
			};

		// We don't need a free function since all descriptors are freed with the descriptorHeap, and we're shutting down Imgui together with the whole application
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {};

		ImGui_ImplDX12_Init(&init_info);
	}

	void imguiShutdown()
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void imguiNewFrame()
	{
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void imguiToggleMouse(bool enabled)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (enabled)
		{
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		}
		else
		{
			io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		}
	}
}
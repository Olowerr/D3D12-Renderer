#pragma once

#include "GPUResourceManager.h"
#include "CommandContext.h"
#include "DescriptorHeapStore.h"


/*
	Render Pass:
	* Shaders
	* Render Targets
		* Get viewport & sciccor rect from this, in future can pass in if need to be different
	* Depth enabled
	* Topology Type
	* FillMode
	* CullMode

	* For now we can assume all entities should pass through the same passes.
	  Later we'll pivot to some entities using some passes, and other entities using other passes.
*/

namespace Okay
{
	class RenderPass
	{
	public:
		RenderPass() = default;
		virtual ~RenderPass() = default;

		void initialize(ID3D12Device* pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineDesc, const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc);
		void shutdown();

		void bind(ID3D12GraphicsCommandList* pDirectCommandList, uint32_t numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRtvHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pDsvHandle, uint32_t numViewports);
		void bindBase(ID3D12GraphicsCommandList* pDirectCommandList);
		void bindRTVs(ID3D12GraphicsCommandList* pCommandList, uint32_t numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRtvHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pDsvHandle, uint32_t numViewports);

		void updateProperties(D3D12_VIEWPORT viewport, D3D12_RECT scissorRect, D3D12_PRIMITIVE_TOPOLOGY topology);

	private:
		void recordBundle(D3D12_PRIMITIVE_TOPOLOGY topology);
		void createPSO(ID3D12Device* pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineDesc);

	private:
		ID3D12GraphicsCommandList* m_pCommandBundle = nullptr;
		ID3D12CommandAllocator* m_pCommandAllocator = nullptr;

		ID3D12RootSignature* m_pRootSignature = nullptr;
		ID3D12PipelineState* m_pPSO = nullptr;

		D3D12_VIEWPORT m_viewport[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
		D3D12_RECT m_scissorRect[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
	};
}

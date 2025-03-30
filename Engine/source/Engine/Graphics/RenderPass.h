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

		//void attachViews()

		void bind(ID3D12GraphicsCommandList* pDirectCommandList);
		// TODO: Add 'setRenderTargets" function (can change name) but point is to move the RTV responsibility to the RenderPasses

	private:
		void recordBundleCommands();

		void createPSO(ID3D12Device* pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineDesc);

	private:
		ID3D12GraphicsCommandList* m_pCommandBundle = nullptr;
		
		ID3D12RootSignature* m_pRootSignature = nullptr;
		ID3D12PipelineState* m_pPSO = nullptr;
	};
}

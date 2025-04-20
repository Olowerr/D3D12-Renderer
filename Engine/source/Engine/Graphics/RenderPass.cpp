#include "RenderPass.h"

namespace Okay
{
	void RenderPass::initialize(ID3D12Device* pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineDesc, const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc)
	{
		m_pRootSignature = createRootSignature(pDevice, rootSignatureDesc);
		createPSO(pDevice, pipelineDesc);

		ID3D12CommandAllocator* pCommandAllocator = nullptr;
		DX_CHECK(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&pCommandAllocator)));
		DX_CHECK(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandBundle)));

		D3D12_RELEASE(pCommandAllocator); // myb works if dx12 holds an internal reference to it :spinthink:

		// NOTE: Bundles do not work with RenderDoc
		recordBundleCommands();
	}

	void RenderPass::shutdown()
	{
		D3D12_RELEASE(m_pCommandBundle);
		D3D12_RELEASE(m_pRootSignature);
		D3D12_RELEASE(m_pPSO);
	}

	void RenderPass::bind(ID3D12GraphicsCommandList* pDirectCommandList)
	{
		pDirectCommandList->ExecuteBundle(m_pCommandBundle);
	}

	void RenderPass::recordBundleCommands()
	{
		// Very empty :eyes:

		m_pCommandBundle->SetGraphicsRootSignature(m_pRootSignature);
		m_pCommandBundle->SetPipelineState(m_pPSO);

		m_pCommandBundle->Close();
	}

	void RenderPass::createPSO(ID3D12Device* pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineDesc)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineDesc;
		psoDesc.pRootSignature = m_pRootSignature;
		
		DX_CHECK(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPSO)));
	}
}

#include "RenderPass.h"

namespace Okay
{
	void RenderPass::initialize(ID3D12Device* pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineDesc, const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc)
	{
		m_pRootSignature = createRootSignature(pDevice, rootSignatureDesc);
		createPSO(pDevice, pipelineDesc);

		DX_CHECK(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&m_pCommandAllocator)));
		DX_CHECK(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, m_pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandBundle)));

		m_pCommandBundle->Close();
	}

	void RenderPass::shutdown()
	{
		D3D12_RELEASE(m_pCommandBundle);
		D3D12_RELEASE(m_pCommandAllocator);
		D3D12_RELEASE(m_pRootSignature);
		D3D12_RELEASE(m_pPSO);
	}

	void RenderPass::bind(ID3D12GraphicsCommandList* pDirectCommandList, uint32_t numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRtvHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pDsvHandle, uint32_t numViewports)
	{
		bindBase(pDirectCommandList);
		bindRTVs(pDirectCommandList, numRTVs, pRtvHandles, pDsvHandle, numViewports);
	}

	void RenderPass::updateProperties(D3D12_VIEWPORT viewport, D3D12_RECT scissorRect, D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		for (uint32_t i = 0; i < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; i++)
		{
			m_viewport[i] = viewport;
			m_scissorRect[i] = scissorRect;
		}
		
		//recordBundle(topology);
	}

	void RenderPass::bindBase(ID3D12GraphicsCommandList* pDirectCommandList)
	{
		//pDirectCommandList->ExecuteBundle(m_pCommandBundle);

		pDirectCommandList->SetGraphicsRootSignature(m_pRootSignature);
		pDirectCommandList->SetPipelineState(m_pPSO);
		pDirectCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	void RenderPass::bindRTVs(ID3D12GraphicsCommandList* pCommandList, uint32_t numRTVs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRtvHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pDsvHandle, uint32_t numViewports)
	{
		// Not supported in bundles
		// https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles#command-list-api-restrictions
		pCommandList->OMSetRenderTargets(numRTVs, pRtvHandles, false, pDsvHandle);
		pCommandList->RSSetViewports(numViewports, m_viewport);
		pCommandList->RSSetScissorRects(numViewports, m_scissorRect);
	}

	void RenderPass::recordBundle(D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX_CHECK(m_pCommandAllocator->Reset());
		DX_CHECK(m_pCommandBundle->Reset(m_pCommandAllocator, nullptr));

		m_pCommandBundle->SetGraphicsRootSignature(m_pRootSignature);
		m_pCommandBundle->SetPipelineState(m_pPSO);
		m_pCommandBundle->IASetPrimitiveTopology(topology);

		m_pCommandBundle->Close();
	}

	void RenderPass::createPSO(ID3D12Device* pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineDesc)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineDesc;
		psoDesc.pRootSignature = m_pRootSignature;
		
		DX_CHECK(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPSO)));
	}
}

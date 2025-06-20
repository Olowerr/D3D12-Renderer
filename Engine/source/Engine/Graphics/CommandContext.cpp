#include "CommandContext.h"

namespace Okay
{
	void CommandContext::initialize(ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue)
	{
		m_pCommandQueue = pCommandQueue;

		D3D12_COMMAND_QUEUE_DESC desc = pCommandQueue->GetDesc();

		DX_CHECK(pDevice->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(&m_pCommandAllocator)));
		DX_CHECK(pDevice->CreateCommandList(0, desc.Type, m_pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandList)));

		DX_CHECK(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));
		m_fenceValue = 0;
	}

	void CommandContext::shutdown()
	{
		D3D12_RELEASE(m_pCommandList);
		D3D12_RELEASE(m_pCommandAllocator);
		D3D12_RELEASE(m_pFence);
	}

	ID3D12GraphicsCommandList* CommandContext::getCommandList()
	{
		return m_pCommandList;
	}

	void CommandContext::flush()
	{
		execute();
		signal();
		wait();
		reset();
	}

	void CommandContext::signal()
	{
		m_fenceValue++;
		DX_CHECK(m_pCommandQueue->Signal(m_pFence, m_fenceValue));
	}

	void CommandContext::execute()
	{
		DX_CHECK(m_pCommandList->Close());
		m_pCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&m_pCommandList);
	}

	void CommandContext::wait()
	{
		if (m_pFence->GetCompletedValue() == m_fenceValue)
			return;

		HANDLE eventHandle = CreateEventEx(nullptr, 0, 0, EVENT_ALL_ACCESS);
		DX_CHECK(m_pFence->SetEventOnCompletion(m_fenceValue, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	void CommandContext::reset()
	{
		DX_CHECK(m_pCommandAllocator->Reset());
		DX_CHECK(m_pCommandList->Reset(m_pCommandAllocator, nullptr));
	}

	void CommandContext::transitionResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES oldState, D3D12_RESOURCE_STATES newState)
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = pResource;
		barrier.Transition.StateBefore = oldState;
		barrier.Transition.StateAfter = newState;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		m_pCommandList->ResourceBarrier(1, &barrier);
	}

	void CommandContext::transitionSubresource(ID3D12Resource* pResource, uint32_t subresource, D3D12_RESOURCE_STATES oldState, D3D12_RESOURCE_STATES newState)
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = pResource;
		barrier.Transition.StateBefore = oldState;
		barrier.Transition.StateAfter = newState;
		barrier.Transition.Subresource = subresource;

		m_pCommandList->ResourceBarrier(1, &barrier);
	}

	void CommandContext::transitionSubresources(ID3D12Resource* pResource, uint32_t* pSubIndicies, uint32_t numSubresources, D3D12_RESOURCE_STATES oldState, D3D12_RESOURCE_STATES newState)
	{
		OKAY_ASSERT(numSubresources < 16);
		D3D12_RESOURCE_BARRIER barriers[16] = {}; // no way we'll need more than this right?
		
		for (uint32_t i = 0; i < numSubresources; i++)
		{
			barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barriers[i].Transition.pResource = pResource;
			barriers[i].Transition.StateBefore = oldState;
			barriers[i].Transition.StateAfter = newState;
			barriers[i].Transition.Subresource = pSubIndicies[i];
		}

		m_pCommandList->ResourceBarrier(numSubresources, barriers);
	}
}
#include "CommandContext.h"
#include "Engine/Okay.h"

namespace Okay
{
	CommandContext::~CommandContext()
	{
		shutdown();
	}

	void CommandContext::initialize(ID3D12Device* pDevice)
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		DX_CHECK(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocator)));
		DX_CHECK(pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue)));
		DX_CHECK(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandList)));

		DX_CHECK(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));
		m_fenceValue = 0;
	}

	void CommandContext::shutdown()
	{
		D3D12_RELEASE(m_pCommandList);
		D3D12_RELEASE(m_pCommandQueue);
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
		wait();
		reset();
	}

	void CommandContext::execute()
	{
		DX_CHECK(m_pCommandList->Close());
		m_pCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&m_pCommandList);
	}

	void CommandContext::wait()
	{
		m_fenceValue++;
		DX_CHECK(m_pCommandQueue->Signal(m_pFence, m_fenceValue));

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
		barrier.Transition.Subresource = 0;

		m_pCommandList->ResourceBarrier(1, &barrier);
	}
}

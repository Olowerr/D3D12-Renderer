#pragma once

#include "Engine/OkayD3D12.h"

namespace Okay
{
	class CommandContext // atm only for graphics command lists
	{
	public:
		CommandContext() = default;
		virtual ~CommandContext() = default;
	
		void initialize(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type);
		void shutdown();
	
		ID3D12GraphicsCommandList* getCommandList();
		ID3D12CommandQueue* getCommandQueue();

		void flush();
	
		void execute();
		void wait();
		void reset();

		void transitionResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES oldState, D3D12_RESOURCE_STATES newState);
		void transitionSubresource(ID3D12Resource* pResource, uint32_t subresource, D3D12_RESOURCE_STATES oldState, D3D12_RESOURCE_STATES newState);
		void transitionSubresources(ID3D12Resource* pResource, uint32_t* pSubIndicies, uint32_t numSubresources, D3D12_RESOURCE_STATES oldState, D3D12_RESOURCE_STATES newState);
	
	private:
		ID3D12GraphicsCommandList* m_pCommandList = nullptr;
		ID3D12CommandQueue* m_pCommandQueue = nullptr;
		ID3D12CommandAllocator* m_pCommandAllocator = nullptr;

		ID3D12Fence* m_pFence = nullptr;
		uint64_t m_fenceValue = INVALID_UINT32;
	};
}

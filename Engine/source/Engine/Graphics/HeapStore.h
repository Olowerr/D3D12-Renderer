#pragma once

#include "Engine/OkayD3D12.h"

#include <d3d12.h>

#define RESOURCE_PLACEMENT_ALIGNMENT	D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
#define BUFFER_DATA_ALIGNMENT			D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
#define TEXTURE_DATA_ALIGNMENT			D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT

namespace Okay
{
	struct Heap
	{
		ID3D12Heap* pHeap = nullptr;
		uint64_t usedHeapSize = INVALID_UINT64;
	};

	class HeapStore
	{
	public:
		HeapStore() = default;
		virtual ~HeapStore() = default;

		void initialize(ID3D12Device* pDevice, D3D12_HEAP_TYPE heapType, uint64_t heapCreationSize);
		void shutdown();

		ID3D12Resource* requestResource(uint64_t width, uint32_t height, uint32_t mips, DXGI_FORMAT format, D3D12_CLEAR_VALUE* pClearValue, bool isDepth);

	private:
		Heap& getSufficientHeap(uint64_t requiredSize); // Rename? "Sufficient" feels odd
		Heap& createNewHeap(uint64_t heapSize);

	private:
		ID3D12Device* m_pDevice = nullptr;

		std::vector<Heap> m_heaps;
		D3D12_HEAP_TYPE m_heapType = D3D12_HEAP_TYPE_DEFAULT;
		uint64_t m_heapCreationSize = INVALID_UINT64;
	};
}

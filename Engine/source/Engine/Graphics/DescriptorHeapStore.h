#pragma once

#include "Engine/OkayD3D12.h"

namespace Okay
{
	typedef uint32_t DescriptorHandle;

	struct DescriptorHeap
	{
		ID3D12DescriptorHeap* pDXDescriptorHeap = nullptr;

		uint16_t usedDescriptorSlots = INVALID_UINT16;
		uint16_t maxDescriptors = INVALID_UINT16;
	};

	class DescriptorHeapStore
	{
	public:
		DescriptorHeapStore() = default;
		virtual ~DescriptorHeapStore() = default;

		void initialize(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, uint16_t numSlots);
		void shutdown();

		DescriptorHandle createDescriptors(uint32_t numDescriptors, const DescriptorDesc* pDescs);

		D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle(DescriptorHandle handle);
		D3D12_GPU_DESCRIPTOR_HANDLE getGPUHandle(DescriptorHandle handle);

	private:
		uint16_t getSufficientDescriptorHeap(uint16_t slots);
		uint16_t createNewDescriptorHeap(uint16_t slots);

		DescriptorHandle encodeHandle(uint16_t heapindex, uint16_t descriptorIndex);
		void decodeHandle(DescriptorHandle handle, DescriptorHeap** ppOutDescriptorHeap, uint16_t* pOutDescriptorIndex);
		void validateDecodedHandle(uint16_t heapIndex, uint16_t descriptorIndex);

	private:
		ID3D12Device* m_pDevice = nullptr;

		std::vector<DescriptorHeap> m_descriptorHeaps;
		D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		uint16_t m_creationSlots = INVALID_UINT16;

		uint32_t m_incrementSize = INVALID_UINT32;
	};
}

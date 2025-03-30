#pragma once

#include "Engine/OkayD3D12.h"

#include <vector>

#define OKAY_DESCRIPTOR_APPEND INVALID_UINT32

namespace Okay
{
	typedef uint16_t DescriptorHeapHandle;

	constexpr DescriptorHeapHandle INVALID_DHH = INVALID_UINT16;

	struct DescriptorHeap
	{
		ID3D12DescriptorHeap* pDXDescriptorHeap = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;

		uint32_t maxDescriptors = INVALID_UINT16;
		uint32_t nextAppendSlot = INVALID_UINT16;

		uint32_t incrementSize = INVALID_UINT32;

		bool committed = false;
	};

	struct Descriptor // myb rework to hold the GPU/CPU handles?
	{
		DescriptorHeapHandle heapHandle = INVALID_DHH;
		uint32_t heapSlot = INVALID_UINT32;
	};

	class DescriptorHeapStore
	{
	public:
		DescriptorHeapStore() = default;
		virtual ~DescriptorHeapStore() = default;

		void initialize(ID3D12Device* pDevice, uint32_t committedHeapStdSize);
		void shutdown();

		DescriptorHeapHandle createDescriptorHeap(uint32_t numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type);
		Descriptor allocateDescriptors(DescriptorHeapHandle heapHandle, uint32_t slotOffset, const DescriptorDesc* pDescs, uint32_t numDescriptors);

		Descriptor allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, const DescriptorDesc* pDescs, uint32_t numDescriptors);

		ID3D12DescriptorHeap* getDXDescriptorHeap(DescriptorHeapHandle heapHandle);

		D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle(const Descriptor& descriptor);
		D3D12_GPU_DESCRIPTOR_HANDLE getGPUHandle(const Descriptor& descriptor);

	private:
		DescriptorHeapHandle findSufficentCommittedHeap(uint32_t numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type);
		DescriptorHeapHandle createDescriptorHeap_Internal(uint32_t numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool committed);

		void validateDescriptorHeapHandle(DescriptorHeapHandle handle);

	private:
		ID3D12Device* m_pDevice = nullptr;

		std::vector<DescriptorHeap> m_descriptorHeaps;
		uint32_t m_committedHeapStdSize = INVALID_UINT32;
	};
}

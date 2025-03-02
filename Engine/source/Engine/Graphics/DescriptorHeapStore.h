#pragma once

#include "Engine/OkayD3D12.h"

namespace Okay
{
	struct DescriptorHeap
	{
		ID3D12DescriptorHeap* pDXDescriptorHeap = nullptr;

		uint32_t usedDescriptorSlots = INVALID_UINT32;
		uint32_t maxDescriptors = INVALID_UINT32;
	};

	class DescriptorHeapStore
	{
	public:
		DescriptorHeapStore() = default;
		virtual ~DescriptorHeapStore() = default;

		void initialize(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numSlots);
		void shutdown();

		uint32_t createDescriptors(uint32_t numDescriptors, const DescriptorDesc* pDescs);

	private:
		DescriptorHeap& getSufficientDescriptorHeap(uint32_t slots);
		DescriptorHeap& createNewDescriptorHeap(uint32_t slots);

	private:
		ID3D12Device* m_pDevice = nullptr;

		std::vector<DescriptorHeap> m_descriptorHeaps;
		D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		uint32_t m_creationSlots = INVALID_UINT32;

		uint32_t m_incrementSize = INVALID_UINT32;
	};
}

#pragma once

#include "Engine/OkayD3D12.h"

namespace Okay
{
	class RingBuffer
	{
	public:
		RingBuffer() = default;
		virtual ~RingBuffer() = default;

		void initialize(ID3D12Device* pDevice, uint64_t size);
		void shutdown();

		D3D12_GPU_VIRTUAL_ADDRESS allocate(const void* pData, uint64_t byteWidth);

		uint8_t* map();
		void unmap(uint64_t bytesWritten);

		D3D12_GPU_VIRTUAL_ADDRESS getCurrentGPUAddress() const;
		uint64_t getOffset() const;

		ID3D12Resource* getDXResource() const;
		void jumpToStart();

	private:
		void createBuffer(ID3D12Device* pDevice, uint64_t size);

	private:
		ID3D12Resource* m_pRingBuffer = nullptr;

		uint64_t m_bufferOffset = INVALID_UINT64;
		uint64_t m_maxSize = INVALID_UINT64;

	};
}

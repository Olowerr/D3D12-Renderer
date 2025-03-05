#pragma once

#include "Okay.h"

#include <d3d12.h>
#include <cassert>
#include <inttypes.h>

// Always defined to check condition
#define OKAY_ASSERT2(condition)																	\
	{																							\
		if (!(condition))																		\
		{																						\
			printf("ASSERT FAILED: %s\nFile: %s\nLine: %d\n", #condition, __FILE__, __LINE__);	\
			__debugbreak();																		\
		}																						\
	}0


#define DX_CHECK(x) OKAY_ASSERT2(SUCCEEDED(x))
#define D3D12_RELEASE(x) if (x) { x->Release(); x = nullptr; }0

namespace Okay
{
	constexpr uint64_t alignAddress64(uint64_t adress, uint32_t alignment)
	{
		return ((adress - 1) - ((adress - 1) % alignment)) + alignment;
	}

	constexpr uint32_t alignAddress32(uint32_t address, uint32_t alignment)
	{
		return (uint32_t)alignAddress64((uint64_t)address, alignment);
	}

	enum DescriptorType : uint32_t
	{
		OKAY_DESCRIPTOR_TYPE_NONE = 0,
		OKAY_DESCRIPTOR_TYPE_SRV = 1,
		OKAY_DESCRIPTOR_TYPE_CBV = 2,
		OKAY_DESCRIPTOR_TYPE_UAV = 3,
		OKAY_DESCRIPTOR_TYPE_RTV = 4,
		OKAY_DESCRIPTOR_TYPE_DSV = 5,
	};

	struct DescriptorDesc
	{
		DescriptorType type = OKAY_DESCRIPTOR_TYPE_NONE;
		union
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
			D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		};

		bool nullDesc = true; // convenience, true means nullptr desc argument during creation
		ID3D12Resource* pDXResource = nullptr; // Some views need the resource for creation
	};

	enum TextureFlags : uint32_t
	{
		OKAY_TEXTURE_FLAG_NONE = 0,
		OKAY_TEXTURE_FLAG_RENDER = 1,
		OKAY_TEXTURE_FLAG_SHADER_READ = 2,
		OKAY_TEXTURE_FLAG_DEPTH = 4,
	};

	enum BufferUsage : uint8_t
	{
		OKAY_BUFFER_USAGE_NONE = 0,
		OKAY_BUFFER_USAGE_STATIC = 1,
		OKAY_BUFFER_USAGE_DYNAMIC = 2,
	};
}
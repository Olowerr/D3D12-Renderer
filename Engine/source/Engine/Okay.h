#pragma once

#include "glm/glm.hpp"

#include <inttypes.h>
#include <cassert>

// Will be defined to not check in dist builds
#define OKAY_ASSERT(condition)																	\
	{																							\
		if (!(condition))																		\
		{																						\
			printf("ASSERT FAILED: %s\nFile: %s\nLine: %d\n", #condition, __FILE__, __LINE__);	\
			__debugbreak();																		\
		}																						\
	}0

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

#define CHECK_BIT(num, pos)	((num) & 1<<(pos))

namespace Okay
{
	struct Vertex
	{
		glm::vec3 position;
	};

	constexpr uint16_t INVALID_UINT16 = UINT16_MAX;
	constexpr uint32_t INVALID_UINT32 = UINT32_MAX;
	constexpr uint64_t INVALID_UINT64 = UINT64_MAX;

	constexpr uint64_t alignAddress64(uint64_t adress, uint32_t alignment)
	{
		return ((adress - 1) - ((adress - 1) % alignment)) + alignment;
	}

	constexpr uint32_t alignAddress32(uint32_t address, uint32_t alignment)
	{
		return (uint32_t)alignAddress64((uint64_t)address, alignment);
	}
}

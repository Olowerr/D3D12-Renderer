#pragma once
#include "Engine/Okay.h"

namespace Okay
{
	template<typename T, uint32_t MaxSize>
	class StaticContainer
	{
	public:
		StaticContainer() = default;
		virtual ~StaticContainer() = default;

		template<typename... Args>
		inline T& emplace(Args&&... args)
		{
			OKAY_ASSERT(m_usedSize < MaxSize);
			return m_storage[m_usedSize++] = T(args...);
		}

		inline uint32_t getSize() const
		{
			return m_usedSize;
		}

		constexpr uint32_t getMaxSize() const
		{
			return MaxSize;
		}

		inline T& operator[](uint32_t index)
		{
			OKAY_ASSERT(index < MaxSize);
			return m_storage[index];
		}

	public:
		uint32_t m_numActive = 0;

	private:
		uint32_t m_usedSize = 0;
		T m_storage[MaxSize];

	};
}

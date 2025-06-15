#pragma once
#include <vector>

namespace Okay
{
	template<typename T>
	struct ActiveVector // Maybe better to just inherit from std::vector ?, kinda like we're extending it's functionality?
	{
		ActiveVector() = default;

		T& operator[](size_t index)
		{
			return list[index];
		}

		const T& operator[](size_t index) const
		{
			return list[index];
		}

		std::vector<T> list;
		uint32_t numActive = 0;
	};
}

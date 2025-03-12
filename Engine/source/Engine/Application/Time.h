#pragma once

#include <chrono>

namespace Okay
{
	typedef float TimeStep;
	typedef std::chrono::duration<TimeStep> Duration;
	typedef std::chrono::system_clock::time_point TimePoint;

	class Timer
	{
	public:
		Timer()
		{
			m_start = std::chrono::system_clock::now();
		}

		virtual ~Timer() = default;

		inline void reset()
		{
			m_start = std::chrono::system_clock::now();
		}

		inline TimeStep measure() const
		{
			Duration duration = std::chrono::system_clock::now() - m_start;
			return duration.count();
		}

	private:
		TimePoint m_start;

	};
}
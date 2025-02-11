#pragma once

#include "Window.h"

namespace Okay
{
	class Application
	{
	public:
		Application(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight);
		virtual ~Application();

		void run();

	private:

		Okay::Window m_window;

	};
}

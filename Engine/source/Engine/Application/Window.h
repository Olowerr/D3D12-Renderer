#pragma once

#include "Engine/Okay.h"

#include "GLFW/glfw3.h"

#include <string_view>
#include <inttypes.h>

namespace Okay
{
	class Window
	{
	public:
		Window() = default;
		virtual ~Window();

		void initiate(std::string_view, uint32_t windowWidth, uint32_t windowHeight);
		void shutdown();

		bool isOpen() const;
		void processMessages();

	private:

		GLFWwindow* m_pGlfwWindow = nullptr;

	};
}

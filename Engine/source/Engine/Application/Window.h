#pragma once
#include "Engine/Okay.h"
#include "Input.h"

#include "GLFW/glfw3.h"

#include <windows.h>
#include <string_view>

namespace Okay
{
	class Window
	{
	public:
		friend class Input;

		Window() = default;
		virtual ~Window();

		void initiate(std::string_view windowTitle, uint32_t windowWidth, uint32_t windowHeight);
		void shutdown();

		bool isOpen() const;
		void processMessages();

		HWND getHWND() const;
		GLFWwindow* getGLFWWindow() const;

		void setWindowTitle(std::string_view newTitle);

	private:
		void setInputMode(MouseMode mode);
		MouseMode getInputMode();

	private:
		GLFWwindow* m_pGlfwWindow = nullptr;
	};
}

#include "Window.h"

namespace Okay
{
	Window::~Window()
	{
		shutdown();
	}

	void Window::initiate(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	{
		m_pGlfwWindow = glfwCreateWindow((int)windowWidth, (int)windowHeight, windowName.data(), nullptr, nullptr);
		OKAY_ASSERT(m_pGlfwWindow);

		if (GLFWmonitor* pMonitor = glfwGetPrimaryMonitor())
		{
			int monitorWidth = -1, monitorHeight = -1;

			glfwGetMonitorWorkarea(pMonitor, nullptr, nullptr, &monitorWidth, &monitorHeight);
			glfwSetWindowPos(m_pGlfwWindow, int((monitorWidth - windowWidth) * 0.5f), int((monitorHeight - windowHeight) * 0.5f));
		}

		glfwShowWindow(m_pGlfwWindow);
	}

	void Window::shutdown()
	{
		if (m_pGlfwWindow)
		{
			glfwDestroyWindow(m_pGlfwWindow);
			m_pGlfwWindow = nullptr;
		}
	}

	bool Window::isOpen() const
	{
		return !glfwWindowShouldClose(m_pGlfwWindow);
	}

	void Window::processMessages()
	{
		glfwPollEvents();
	}

}

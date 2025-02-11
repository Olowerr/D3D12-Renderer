#include "Application.h"

namespace Okay
{
	Application::Application(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	{
		glfwInitHint(GLFW_CLIENT_API, GLFW_NO_API);

		bool glInit = glfwInit();
		OKAY_ASSERT(glInit);

		m_window.initiate(windowName, windowWidth, windowHeight);
	}

	Application::~Application()
	{
		m_window.shutdown();
	}

	void Application::run()
	{
		while (m_window.isOpen())
		{
			m_window.processMessages();
		}
	}
}

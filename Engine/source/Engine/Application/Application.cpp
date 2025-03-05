#include "Application.h"

namespace Okay
{
	Application::Application(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	{
		glfwInitHint(GLFW_CLIENT_API, GLFW_NO_API);

		bool glInit = glfwInit();
		OKAY_ASSERT(glInit);

		m_window.initiate(windowName, windowWidth, windowHeight);
		m_renderer.initialize(m_window);
	}

	Application::~Application()
	{
		m_renderer.shutdown();
		m_window.shutdown();
	}

	void Application::run()
	{
		while (m_window.isOpen())
		{
			m_window.processMessages();

			onUpdate(0.f); // 0.f dt temporary

			m_renderer.render(m_scene);
		}
	}
}

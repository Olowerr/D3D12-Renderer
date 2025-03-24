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
		m_renderer.preProcessResources(m_resourceManager);
		m_resourceManager.unloadCPUData();

		Timer frameTimer;

		while (m_window.isOpen())
		{
			TimeStep timeStep = frameTimer.measure();
			frameTimer.reset();

			m_window.processMessages();

			onUpdate(timeStep);

			m_renderer.render(m_scene);
		}
	}
}

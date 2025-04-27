#include "Application.h"

#include "ImguiHelper.h"

namespace Okay
{
	Application::Application(std::string_view windowTitle, uint32_t windowWidth, uint32_t windowHeight)
	{
		glfwInitHint(GLFW_CLIENT_API, GLFW_NO_API);

		bool glInit = glfwInit();
		OKAY_ASSERT(glInit);

		m_window.initiate(windowTitle, windowWidth, windowHeight);
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

			imguiNewFrame();

			onUpdate(timeStep);

			m_renderer.render(m_scene);
		}
	}

	void Application::createEntitesFromFile(FilePath path, float scale)
	{
		std::vector<LoadedObject> objects;
		m_resourceManager.loadObjects(path, objects, scale);

		for (LoadedObject& objectData : objects)
		{
			Entity entity = m_scene.createEntity();
			
			entity.getComponent<Transform>().setFromMatrix(objectData.transformMatrix);

			MeshRenderer& meshRenderer = entity.addComponent<MeshRenderer>();
			meshRenderer.meshID = objectData.meshID;
			meshRenderer.diffuseTextureID = objectData.diffuseTextureID;
			meshRenderer.normalMapID = objectData.normalMapID;
		}
	}
}

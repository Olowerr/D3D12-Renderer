#include "App.h"

App::App(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	:Okay::Application(windowName, windowWidth, windowHeight)
{
	Okay::Entity camEntity = m_scene.createEntity();
	camEntity.addComponent<Okay::Camera>();

	m_scene.setActiveCamera(camEntity);

	camEntity.getComponent<Okay::Transform>().position = glm::vec3(1.f, 0.f, -1.f);
	camEntity.getComponent<Okay::Transform>().rotation = glm::vec3(0.f, -45.f, 0.f);
}

App::~App()
{

}

void App::onUpdate(float dt)
{
}
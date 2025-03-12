#include "App.h"

using namespace Okay;

App::App(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	:Application(windowName, windowWidth, windowHeight)
{
	m_camEntity = m_scene.createEntity();
	m_camEntity.addComponent<Camera>();

	m_scene.setActiveCamera(m_camEntity);
}

App::~App()
{

}

void App::onUpdate(TimeStep dt)
{
	Transform& camTransform = m_camEntity.getComponent<Transform>();

	camTransform.rotation.x = 20.f;
	camTransform.rotation.y += 45.f * dt;

	camTransform.position = camTransform.forwardVec() * -2.f;
}

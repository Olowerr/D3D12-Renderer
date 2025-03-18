#include "App.h"

using namespace Okay;

App::App(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	:Application(windowName, windowWidth, windowHeight)
{
	m_camEntity = m_scene.createEntity();
	m_camEntity.addComponent<Camera>();

	m_scene.setActiveCamera(m_camEntity);

	for (uint32_t i = 0; i < 3; i++)
	{
		Entity entity = m_scene.createEntity();

		entity.getComponent<Transform>().position.x = (float)i - 3 * 0.5f + 0.5f;
		entity.getComponent<Transform>().rotation.y = i * 180.f;

		entity.addComponent<MeshRenderer>();
	}
}

App::~App()
{

}

void App::onUpdate(TimeStep dt)
{
	Transform& camTransform = m_camEntity.getComponent<Transform>();

	camTransform.rotation.x = 20.f;
	camTransform.rotation.y += 45.f * dt;

	camTransform.position = camTransform.forwardVec() * -3.f;
}

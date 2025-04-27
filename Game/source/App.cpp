#include "App.h"

#include "imgui/imgui.h"

using namespace Okay;

App::App(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	:Application(windowName, windowWidth, windowHeight)
{
	createEntitesFromFile(FilePath("resources") / "sponza" / "sponza.obj", 1.f);

	m_camEntity = m_scene.createEntity();
	m_camEntity.addComponent<Camera>();
	m_scene.setActiveCamera(m_camEntity);

	PointLight& pointLight = m_camEntity.addComponent<PointLight>();
	pointLight.colour = glm::vec3(0.9f, 0.2f, 0.4f);
	pointLight.intensity = 10.f;

	Entity sun = m_scene.createEntity();
	sun.getComponent<Transform>().rotation = glm::vec3(45.f, 45.f, 0.f);

	DirectionalLight& dirLight = sun.addComponent<DirectionalLight>();
	dirLight.colour = glm::vec3(246.f, 163.f, 22.f) / 255.f;
	dirLight.intensity = 1.f;


	Entity spotLightEntity = m_scene.createEntity();
	spotLightEntity.getComponent<Transform>().position = glm::vec3(847.f, 366.f, -166.f);
	spotLightEntity.getComponent<Transform>().rotation = glm::vec3(27.1f, -62.5f, 0.f);
	
		
	SpotLight& spotLight = spotLightEntity.addComponent<SpotLight>();
	spotLight.colour = glm::vec3(0.3f, 0.5f, 0.9f);
	spotLight.intensity = 1.f;
	spotLight.attenuation = glm::vec2(0.f, 0.000001f);
	spotLight.spreadAngle = 60.f;
}

App::~App()
{

}

void App::onUpdate(TimeStep dt)
{
	updateCamera(dt);

	ImGui::ShowDemoWindow(nullptr);
}

void App::updateCamera(TimeStep dt)
{
	if (Input::isKeyPressed(Key::E))
	{
		MouseMode newMode = Input::getMouseMode() == MouseMode::LOCKED ? MouseMode::FREE : MouseMode::LOCKED;
		Input::setMouseMode(newMode);
	}

	if (Input::getMouseMode() == MouseMode::FREE)
	{
		return;
	}

	Transform& camTransform = m_camEntity.getComponent<Transform>();

	float camMoveSpeed = 300.f;
	float camRotSpeed = 0.1f;


	// Movement

	float forwardDir = (float)Input::isKeyDown(Key::W) - (float)Input::isKeyDown(Key::S);
	float rightDir = (float)Input::isKeyDown(Key::D) - (float)Input::isKeyDown(Key::A);
	float upDir = (float)Input::isKeyDown(Key::SPACE) - (float)Input::isKeyDown(Key::L_SHIFT);

	glm::vec3 moveDir = glm::vec3(0.f);

	if (forwardDir)
	{
		moveDir += camTransform.forwardVec() * forwardDir;
	}
	if (rightDir)
	{
		moveDir += camTransform.rightVec() * rightDir;
	}
	if (upDir)
	{
		moveDir += camTransform.upVec() * upDir;
	}

	if (forwardDir || rightDir || upDir)
	{
		camTransform.position += glm::normalize(moveDir) * camMoveSpeed * dt;
	}


	// Rotation

	glm::vec2 mouseDelta = Input::getMouseDelta();

	camTransform.rotation.y += mouseDelta.x * camRotSpeed;
	camTransform.rotation.x += mouseDelta.y * camRotSpeed;
}

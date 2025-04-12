#include "App.h"

using namespace Okay;

App::App(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	:Application(windowName, windowWidth, windowHeight)
{
	m_camEntity = m_scene.createEntity();
	m_camEntity.addComponent<Camera>();

	m_scene.setActiveCamera(m_camEntity);

	m_resourceManager.loadMesh(FilePath("resources") / "Meshes" / "Cube.fbx");
	m_resourceManager.loadMesh(FilePath("resources") / "Meshes" / "dragon_80K.obj");
	m_resourceManager.loadMesh(FilePath("resources") / "Meshes" / "sphere.fbx");

	uint32_t numObjects = 3;
	float spaceBetween = 2.f;

	for (uint32_t i = 0; i < 3; i++)
	{
		for (uint32_t k = 0; k < numObjects; k++)
		{
			Entity entity = m_scene.createEntity();

			entity.getComponent<Transform>().position.x = k * spaceBetween - (numObjects - 1) * spaceBetween * 0.5f;
			entity.getComponent<Transform>().position.z = i * spaceBetween - (numObjects - 1) * spaceBetween * 0.5f;

			MeshRenderer& meshRenderer = entity.addComponent<MeshRenderer>();
			meshRenderer.meshID = i;
			meshRenderer.textureID = k + i * numObjects;
		}
	}

	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "quack.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "CATt.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "bigward2.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "CATAC.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "denise.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "deniseCropped.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "ludde.png");
	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "sus.png");
	m_resourceManager.loadTexture(FilePath("resources") / "Textures" / "whenThe.png");
}

App::~App()
{

}

void App::onUpdate(TimeStep dt)
{
	Transform& camTransform = m_camEntity.getComponent<Transform>();

	camTransform.rotation.x = 20.f;
	camTransform.rotation.y += 45.f * dt;

	camTransform.position = camTransform.forwardVec() * -6.f;
}

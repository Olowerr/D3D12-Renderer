#include "App.h"

using namespace Okay;

App::App(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight)
	:Application(windowName, windowWidth, windowHeight)
{
	m_camEntity = m_scene.createEntity();
	m_camEntity.addComponent<Camera>();

	m_scene.setActiveCamera(m_camEntity);

	m_resourceManager.loadMesh(FilePath("resources") / "meshes" / "Cube.fbx");
	m_resourceManager.loadMesh(FilePath("resources") / "meshes" / "dragon_80K.obj");
	m_resourceManager.loadMesh(FilePath("resources") / "meshes" / "sphere.fbx");

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

	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "quack.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "CATt.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "bigward2.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "CATAC.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "denise.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "deniseCropped.jpg");
	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "ludde.png");
	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "sus.png");
	m_resourceManager.loadTexture(FilePath("resources") / "textures" / "whenThe.png");
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

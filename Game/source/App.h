#pragma once

#include "Engine/Application/Application.h"

class App : public Okay::Application
{
public:
	App(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight);
	~App();

	virtual void onUpdate(Okay::TimeStep dt) override;

private:
	void updateCamera(Okay::TimeStep dt);

private:
	Okay::Entity m_camEntity;

};


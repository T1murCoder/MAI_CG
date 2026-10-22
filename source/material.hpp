#pragma once

#include <glm/glm.hpp>

// Параметры материала по модели Блинн-Фонга
struct Material {
	glm::vec3 albedo = glm::vec3(1.0f);   // диффузный цвет Kd (он же цвет в рассеянном свете)
	glm::vec3 specular = glm::vec3(0.5f); // цвет блика Ks
	float shininess = 32.0f;              // коэффициент блеска α: чем больше, тем меньше и резче блик

	// Цвета альбедо и блика, коэффициент блеска в текущем окне ImGui
	void drawUi();
};

#pragma once

#include <glm/glm.hpp>

// Положение объекта в мире: перенос, повороты по осям X/Y/Z (в градусах) и масштаб
struct Transform {
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 rotation_degrees = glm::vec3(0.0f);
	glm::vec3 scale = glm::vec3(1.0f);

	// Матрица модели в порядке TRS: T · Rx · Ry · Rz · S
	glm::mat4 matrix() const;

	// Слайдеры позиции, поворота и масштаба в текущем окне ImGui
	void drawUi();
};

#include "transform.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

// Порядок TRS: перенос, повороты по X/Y/Z, масштаб
glm::mat4 Transform::matrix() const {
	glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
	model = glm::rotate(model, glm::radians(rotation_degrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation_degrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation_degrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::scale(model, scale);
	return model;
}

// Ручная трансформация; во время анимации она служит базой (центр орбиты и начальный поворот)
void Transform::drawUi() {
	ImGui::SeparatorText("Transform");
	ImGui::SliderFloat3("Position", &position.x, -3.0f, 3.0f);
	ImGui::SliderFloat3("Rotation", &rotation_degrees.x, -180.0f, 180.0f);
	ImGui::SliderFloat3("Scale", &scale.x, 0.1f, 3.0f);
}

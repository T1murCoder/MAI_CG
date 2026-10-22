#include "lighting.hpp"

#include <imgui.h>

void AmbientLight::drawUi() {
	ImGui::SeparatorText("Ambient Light");
	ImGui::ColorEdit3("Ambient Color", &color.x);
	ImGui::SliderFloat("Ambient Intensity", &intensity, 0.0f, 1.0f);
}

// Направление задаётся вектором без нормализации — в шейдер уходит уже единичный
void DirectionalLight::drawUi() {
	ImGui::SeparatorText("Directional Light");
	ImGui::Checkbox("Enabled", &enabled);
	ImGui::SliderFloat3("Direction", &direction.x, -1.0f, 1.0f);
	ImGui::ColorEdit3("Light Color", &color.x);
	ImGui::SliderFloat("Light Intensity", &intensity, 0.0f, 5.0f);
}

// Нулевое направление нормализовать нельзя — тогда считаем, что свет падает сверху вниз
void Lighting::fill(shader::SceneUniforms& uniforms) const {
	const bool has_direction = glm::dot(directional.direction, directional.direction) > 0.0f;
	const glm::vec3 direction = has_direction ? glm::normalize(directional.direction)
											  : glm::vec3(0.0f, -1.0f, 0.0f);

	uniforms.ambient = glm::vec4(ambient.color, ambient.intensity);
	uniforms.light_direction = glm::vec4(direction, directional.enabled ? 1.0f : 0.0f);
	uniforms.light_color = glm::vec4(directional.color, directional.intensity);
}

void Lighting::drawUi() {
	ambient.drawUi();
	directional.drawUi();
}

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

// Из-за затухания 1/d² интенсивности нужен большой запас: на расстоянии 3 остаётся 1/9 света
void PointLight::drawUi() {
	ImGui::DragFloat3("Position", &position.x, 0.02f);
	ImGui::ColorEdit3("Color", &color.x);
	ImGui::SliderFloat("Intensity", &intensity, 0.0f, 50.0f);
}

bool Lighting::create(VkDescriptorSet scene_set) {
	return point_light_buffer.create(sizeof(shader::PointLight), scene_set, 1);
}

void Lighting::destroy() {
	point_light_buffer.destroy();
}

// Нулевое направление нормализовать нельзя — тогда считаем, что свет падает сверху вниз
void Lighting::upload(shader::SceneUniforms& uniforms) {
	const bool has_direction = glm::dot(directional.direction, directional.direction) > 0.0f;
	const glm::vec3 direction = has_direction ? glm::normalize(directional.direction)
											  : glm::vec3(0.0f, -1.0f, 0.0f);

	uniforms.ambient = glm::vec4(ambient.color, ambient.intensity);
	uniforms.light_direction = glm::vec4(direction, directional.enabled ? 1.0f : 0.0f);
	uniforms.light_color = glm::vec4(directional.color, directional.intensity);

	// Упаковываем источники в формат GPU (vec3 → vec4) и сообщаем шейдеру, сколько их реально загружено
	std::vector<shader::PointLight> gpu_point_lights;
	gpu_point_lights.reserve(point_lights.size());
	for (const PointLight& light : point_lights) {
		gpu_point_lights.push_back({
			.position = glm::vec4(light.position, 1.0f),
			.color = glm::vec4(light.color, light.intensity),
		});
	}

	uniforms.light_counts.x =
		point_light_buffer.upload(gpu_point_lights.data(), uint32_t(gpu_point_lights.size()));
}

void Lighting::drawUi() {
	ambient.drawUi();
	directional.drawUi();
	drawPointLightsUi();
}

// Удаление откладывается до конца цикла, чтобы не менять вектор во время обхода
void Lighting::drawPointLightsUi() {
	ImGui::SeparatorText("Point Lights");
	if (ImGui::Button("Add Point Light")) {
		point_lights.push_back(PointLight{});
	}

	int remove_index = -1;
	for (int i = 0; i < int(point_lights.size()); ++i) {
		ImGui::PushID(i);
		if (ImGui::TreeNodeEx("light", ImGuiTreeNodeFlags_DefaultOpen, "Point Light %d", i)) {
			point_lights[i].drawUi();
			if (ImGui::Button("Remove")) {
				remove_index = i;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	if (remove_index >= 0) {
		point_lights.erase(point_lights.begin() + remove_index);
	}
}

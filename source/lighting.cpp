#include "lighting.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

namespace {

// Список источников одного типа: кнопка добавления и по ветке дерева на каждый источник с кнопкой
// удаления. Удаление откладывается до конца цикла, чтобы не менять вектор во время обхода
template <typename Light>
void drawLightList(const char* title, const char* item_label, std::vector<Light>& lights) {
	ImGui::PushID(title);
	ImGui::SeparatorText(title);
	if (ImGui::Button("Add")) {
		lights.push_back(Light{});
	}

	int remove_index = -1;
	for (int i = 0; i < int(lights.size()); ++i) {
		ImGui::PushID(i);
		if (ImGui::TreeNodeEx("light", ImGuiTreeNodeFlags_DefaultOpen, "%s %d", item_label, i)) {
			lights[i].drawUi();
			if (ImGui::Button("Remove")) {
				remove_index = i;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	if (remove_index >= 0) {
		lights.erase(lights.begin() + remove_index);
	}
	ImGui::PopID();
}

} // namespace

glm::vec3 normalizeDirection(glm::vec3 direction) {
	return glm::dot(direction, direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
}

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

glm::vec3 PointLight::worldPosition() const {
	return animation.apply({ .position = position }).position;
}

// Из-за затухания 1/d² интенсивности нужен большой запас: на расстоянии 3 остаётся 1/9 света.
// У точечного источника нет ориентации, поэтому скорости вращения в UI анимации скрыты
void PointLight::drawUi() {
	ImGui::DragFloat3("Position", &position.x, 0.02f);
	ImGui::ColorEdit3("Color", &color.x);
	ImGui::SliderFloat("Intensity", &intensity, 0.0f, 50.0f);
	animation.drawUi(false);
}

glm::vec3 SpotLight::worldPosition() const {
	return animation.apply({ .position = position }).position;
}

// Накопленное вращение анимации поворачивает ось конуса: например, вращение вокруг Y
// у наклонённого прожектора заставляет пятно ходить по кругу
glm::vec3 SpotLight::worldDirection() const {
	const Transform rotation = { .rotation_degrees = animation.apply({}).rotation_degrees };
	return normalizeDirection(glm::mat3(rotation.matrix()) * normalizeDirection(direction));
}

// Углы связаны: увеличение inner подтягивает outer, уменьшение outer подтягивает inner,
// поэтому outer >= inner всегда и мягкий край не «выворачивается»
void SpotLight::drawUi() {
	ImGui::DragFloat3("Position", &position.x, 0.02f);
	ImGui::SliderFloat3("Direction", &direction.x, -1.0f, 1.0f);
	ImGui::ColorEdit3("Color", &color.x);
	ImGui::SliderFloat("Intensity", &intensity, 0.0f, 100.0f);
	if (ImGui::SliderFloat("Inner Angle", &inner_degrees, 1.0f, 89.0f)) {
		outer_degrees = std::max(outer_degrees, inner_degrees);
	}
	if (ImGui::SliderFloat("Outer Angle", &outer_degrees, 1.0f, 89.0f)) {
		inner_degrees = std::min(inner_degrees, outer_degrees);
	}
	animation.drawUi();
}

bool Lighting::create(VkDescriptorSet scene_set) {
	return point_light_buffer.create(sizeof(shader::PointLight), scene_set, 1) &&
		   spot_light_buffer.create(sizeof(shader::SpotLight), scene_set, 2);
}

void Lighting::destroy() {
	spot_light_buffer.destroy();
	point_light_buffer.destroy();
}

void Lighting::update(double dt) {
	for (PointLight& light : point_lights) {
		light.animation.update(dt);
	}
	for (SpotLight& light : spot_lights) {
		light.animation.update(dt);
	}
}

// Источники упаковываются в формат GPU (vec3 → vec4, углы → косинусы); шейдеру сообщается,
// сколько источников реально загружено
void Lighting::upload(shader::SceneUniforms& uniforms) {
	uniforms.ambient = glm::vec4(ambient.color, ambient.intensity);
	uniforms.light_direction =
		glm::vec4(normalizeDirection(directional.direction), directional.enabled ? 1.0f : 0.0f);
	uniforms.light_color = glm::vec4(directional.color, directional.intensity);

	std::vector<shader::PointLight> gpu_point_lights;
	gpu_point_lights.reserve(point_lights.size());
	for (const PointLight& light : point_lights) {
		gpu_point_lights.push_back({
			.position = glm::vec4(light.worldPosition(), 1.0f),
			.color = glm::vec4(light.color, light.intensity),
		});
	}

	std::vector<shader::SpotLight> gpu_spot_lights;
	gpu_spot_lights.reserve(spot_lights.size());
	for (const SpotLight& light : spot_lights) {
		gpu_spot_lights.push_back({
			.position = glm::vec4(light.worldPosition(), 1.0f),
			.direction = glm::vec4(light.worldDirection(), 0.0f),
			.color = glm::vec4(light.color, light.intensity),
			.cutoff = glm::vec4(std::cos(glm::radians(light.inner_degrees)),
								std::cos(glm::radians(light.outer_degrees)), 0.0f, 0.0f),
		});
	}

	uniforms.light_counts.x =
		point_light_buffer.upload(gpu_point_lights.data(), uint32_t(gpu_point_lights.size()));
	uniforms.light_counts.y =
		spot_light_buffer.upload(gpu_spot_lights.data(), uint32_t(gpu_spot_lights.size()));
}

void Lighting::drawUi() {
	ambient.drawUi();
	directional.drawUi();
	drawLightList("Point Lights", "Point Light", point_lights);
	drawLightList("Spot Lights", "Spot Light", spot_lights);
}

const std::vector<PointLight>& Lighting::pointLights() const {
	return point_lights;
}

const std::vector<SpotLight>& Lighting::spotLights() const {
	return spot_lights;
}

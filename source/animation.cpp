#include "animation.hpp"

#include <cmath>

#include <imgui.h>

Animation::Animation(bool is_enabled, float radius, float speed, glm::vec3 spin_speed)
	: enabled(is_enabled), orbit_radius(radius), orbit_speed(speed), spin_speed_degrees(spin_speed) {}

// Углы сворачиваются по модулю полного оборота, чтобы при долгой работе не терять точность во float
void Animation::update(double dt) {
	if (!enabled || paused) {
		return;
	}

	orbit_angle_radians = std::fmod(orbit_angle_radians + dt * double(orbit_speed), 2.0 * M_PI);
	spin_angle_degrees = glm::mod(spin_angle_degrees + dt * glm::dvec3(spin_speed_degrees), 360.0);
}

// Позиция смещается по орбите вокруг base.position, поворот добавляется к base.rotation_degrees
Transform Animation::apply(const Transform& base) const {
	if (!enabled) {
		return base;
	}

	Transform result = base;
	result.position += glm::vec3(orbit_radius * float(std::cos(orbit_angle_radians)), 0.0f,
								 orbit_radius * float(std::sin(orbit_angle_radians)));
	result.rotation_degrees += glm::vec3(spin_angle_degrees);
	return result;
}

// Кнопка паузы активна только при включённой анимации; замораживает все накопленные углы на месте
void Animation::drawUi() {
	ImGui::SeparatorText("Animation");
	ImGui::Checkbox("Animate", &enabled);
	ImGui::BeginDisabled(!enabled);
	ImGui::SameLine();
	if (ImGui::Button(paused ? "Play" : "Pause")) {
		paused = !paused;
	}
	ImGui::EndDisabled();
	ImGui::SliderFloat("Orbit Speed", &orbit_speed, 0.0f, 5.0f);
	ImGui::SliderFloat("Radius", &orbit_radius, 0.0f, 5.0f);
	ImGui::SliderFloat("Rotation Speed X", &spin_speed_degrees.x, -360.0f, 360.0f);
	ImGui::SliderFloat("Rotation Speed Y", &spin_speed_degrees.y, -360.0f, 360.0f);
	ImGui::SliderFloat("Rotation Speed Z", &spin_speed_degrees.z, -360.0f, 360.0f);
}

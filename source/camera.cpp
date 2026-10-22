#include "camera.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

namespace {

// Строго 90° нельзя: при взгляде вдоль up векторное произведение cross(front, up) вырождается в ноль
constexpr float kMaxPitchDegrees = 89.0f;

} // namespace

Camera::Camera() {
	lookAt(glm::vec3(0.0f));
}

void Camera::update(double dt) {
	const ImGuiIO& io = ImGui::GetIO();

	// Поворот: ПКМ зажата в области сцены. Движение мыши вверх (отрицательный dy) поднимает взгляд
	if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		yaw_degrees = std::remainder(yaw_degrees + io.MouseDelta.x * mouse_sensitivity, 360.0f);
		pitch_degrees = glm::clamp(pitch_degrees - io.MouseDelta.y * mouse_sensitivity, -kMaxPitchDegrees,
								   kMaxPitchDegrees);
	}

	if (io.WantCaptureKeyboard) {
		return;
	}

	// Перемещение: W/S — вдоль взгляда, A/D — вбок, E/Q — по мировой вертикали
	const glm::vec3 front = forward();
	const glm::vec3 right = glm::normalize(glm::cross(front, up));

	const struct {
		ImGuiKey key;
		glm::vec3 axis;
	} bindings[] = {
		{ ImGuiKey_W, front }, { ImGuiKey_S, -front }, { ImGuiKey_D, right },
		{ ImGuiKey_A, -right }, { ImGuiKey_E, up },    { ImGuiKey_Q, -up },
	};

	glm::vec3 direction(0.0f);
	for (const auto& binding : bindings) {
		if (ImGui::IsKeyDown(binding.key)) {
			direction += binding.axis;
		}
	}

	// Нормализуем, чтобы по диагонали камера не двигалась быстрее, чем по одной оси
	if (glm::dot(direction, direction) > 0.0f) {
		const float speed = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f * move_speed : move_speed;
		position += glm::normalize(direction) * speed * float(dt);
	}
}

glm::mat4 Camera::view() const {
	return glm::lookAt(position, position + forward(), up);
}

// Соотношение сторон передаётся каждый кадр — окно может быть изменено в размере
glm::mat4 Camera::projection(float aspect) const {
	glm::mat4 proj;
	if (perspective) {
		proj = glm::perspective(glm::radians(fov_degrees), aspect, near_plane, far_plane);
	} else {
		// ortho_half_height подобран так, чтобы куб был примерно того же размера, что и в перспективе
		proj = glm::ortho(-ortho_half_height * aspect, ortho_half_height * aspect, -ortho_half_height,
						  ortho_half_height, near_plane, far_plane);
	}

	// В Vulkan clip space ось Y направлена вниз, в отличие от OpenGL, под который заточен GLM
	proj[1][1] *= -1.0f;
	return proj;
}

void Camera::fill(shader::SceneUniforms& uniforms, float aspect) const {
	uniforms.view_projection = projection(aspect) * view();
	uniforms.camera_position = glm::vec4(position, 1.0f);
	uniforms.tone_mapping = glm::vec4(exposure, tone_mapping ? 1.0f : 0.0f, 0.0f, 0.0f);
}

// Слайдер FoV активен только для перспективы, размер видимой области — только для ортографии
void Camera::drawUi() {
	ImGui::SeparatorText("Camera");
	if (ImGui::RadioButton("Perspective", perspective)) {
		perspective = true;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", !perspective)) {
		perspective = false;
	}

	ImGui::DragFloat3("Camera Position", &position.x, 0.05f);
	ImGui::SliderFloat("Yaw", &yaw_degrees, -180.0f, 180.0f);
	ImGui::SliderFloat("Pitch", &pitch_degrees, -kMaxPitchDegrees, kMaxPitchDegrees);
	if (ImGui::Button("Look at origin")) {
		lookAt(glm::vec3(0.0f));
	}

	ImGui::BeginDisabled(!perspective);
	ImGui::SliderFloat("FoV", &fov_degrees, 10.0f, 120.0f);
	ImGui::EndDisabled();

	ImGui::BeginDisabled(perspective);
	ImGui::SliderFloat("Ortho Half Height", &ortho_half_height, 0.5f, 10.0f);
	ImGui::EndDisabled();

	// Экспозиция действует только вместе с тон-маппингом: без него освещённость выше 1 обрезается
	ImGui::Checkbox("Tone Mapping", &tone_mapping);
	ImGui::BeginDisabled(!tone_mapping);
	ImGui::SliderFloat("Exposure", &exposure, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
	ImGui::EndDisabled();

	ImGui::SliderFloat("Move Speed", &move_speed, 0.5f, 20.0f);
	ImGui::SliderFloat("Mouse Sensitivity", &mouse_sensitivity, 0.01f, 1.0f);
	ImGui::TextDisabled("WASD — move, QE — down/up, Shift — faster, RMB + mouse — look");
}

// Сферические координаты: yaw отсчитывается в плоскости XZ от оси +X к +Z, pitch — от этой плоскости вверх
glm::vec3 Camera::forward() const {
	const float yaw = glm::radians(yaw_degrees);
	const float pitch = glm::radians(pitch_degrees);
	return glm::vec3(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
}

// Обратное к forward(): углы восстанавливаются из направления на target
void Camera::lookAt(glm::vec3 target) {
	const glm::vec3 direction = target - position;
	// Цель совпадает с положением камеры — направление не определено, углы остаются прежними
	if (glm::dot(direction, direction) == 0.0f) {
		return;
	}

	const glm::vec3 d = glm::normalize(direction);
	yaw_degrees = glm::degrees(std::atan2(d.z, d.x));
	pitch_degrees = glm::clamp(glm::degrees(std::asin(d.y)), -kMaxPitchDegrees, kMaxPitchDegrees);
}

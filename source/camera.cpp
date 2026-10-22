#include "camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

glm::mat4 Camera::view() const {
	return glm::lookAt(position, target, up);
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

	ImGui::SliderFloat3("Camera Position", &position.x, -10.0f, 10.0f);
	ImGui::SliderFloat3("Camera Target", &target.x, -5.0f, 5.0f);

	ImGui::BeginDisabled(!perspective);
	ImGui::SliderFloat("FoV", &fov_degrees, 10.0f, 120.0f);
	ImGui::EndDisabled();

	ImGui::BeginDisabled(perspective);
	ImGui::SliderFloat("Ortho Half Height", &ortho_half_height, 0.5f, 10.0f);
	ImGui::EndDisabled();
}

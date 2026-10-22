#pragma once

#include <glm/glm.hpp>

// Камера-наблюдатель: смотрит из position в target; проекция перспективная или ортографическая
class Camera {
public:
	// Матрица вида: переводит мировые координаты в систему координат камеры
	glm::mat4 view() const;

	// Матрица проекции под соотношение сторон окна, уже с поправкой на ось Y в clip space Vulkan
	glm::mat4 projection(float aspect) const;

	// Тип проекции, положение, цель и угол обзора камеры в текущем окне ImGui
	void drawUi();

private:
	glm::vec3 position = glm::vec3(2.0f, 2.0f, 3.0f);
	glm::vec3 target = glm::vec3(0.0f);
	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

	bool perspective = true;
	float fov_degrees = 45.0f;      // вертикальный угол обзора перспективной проекции
	float ortho_half_height = 1.7f; // половина высоты видимой области ортографической проекции
	float near_plane = 0.1f;
	float far_plane = 100.0f;
};

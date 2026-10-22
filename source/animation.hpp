#pragma once

#include <glm/glm.hpp>

#include "transform.hpp"

// Процедурная анимация объекта: движение по круговой орбите в плоскости XZ вокруг базовой позиции
// и вращение по каждой из трёх осей; орбита и вращения накапливаются независимо друг от друга
class Animation {
public:
	Animation() = default;
	Animation(bool is_enabled, float radius, float speed, glm::vec3 spin_speed);

	// Продвигает накопленные углы на dt секунд, если анимация включена и не на паузе
	void update(double dt);

	// Применяет текущее состояние анимации к базовой трансформации; выключенная анимация возвращает base
	Transform apply(const Transform& base) const;

	// Чекбокс включения, кнопка паузы и слайдеры скоростей в текущем окне ImGui; with_rotation = false
	// скрывает скорости вращения — для объектов без ориентации (точечный источник)
	void drawUi(bool with_rotation = true);

private:
	// Параметры, задаются конструктором и из UI
	bool enabled = false;
	bool paused = false;
	float orbit_radius = 2.0f;
	float orbit_speed = 1.0f;                                      // радиан в секунду
	glm::vec3 spin_speed_degrees = glm::vec3(0.0f, 90.0f, 0.0f); // градусов в секунду по осям X/Y/Z

	// Накопленное состояние, меняется только в update()
	double orbit_angle_radians = 0.0;
	glm::dvec3 spin_angle_degrees = glm::dvec3(0.0);
};

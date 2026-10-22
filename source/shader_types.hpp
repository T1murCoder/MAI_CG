#pragma once

#include <glm/glm.hpp>

// Структуры, которые копируются в буферы GPU. Это контракт с шейдерами: раскладка каждой структуры
// должна совпадать с одноимённым блоком в shaders/common.glsl.
// Uniform-буферы используют раскладку std140: vec3 там выровнен по 16 байтам, а в C++ занимает 12,
// поэтому в структурах только vec4/mat4; static_assert ловит расхождение размеров при компиляции
namespace shader {

// Общие для всех моделей данные сцены (set 0, binding 0)
struct SceneUniforms {
	glm::mat4 view_projection;
	glm::vec4 camera_position; // xyz — положение камеры в мире (нужно для блика), w не используется
	glm::vec4 ambient;         // rgb — цвет рассеянного света, w — интенсивность
	glm::vec4 light_direction; // xyz — нормализованное направление лучей направленного света, w — 1/0 вкл/выкл
	glm::vec4 light_color;     // rgb — цвет направленного света, w — интенсивность
};
static_assert(sizeof(SceneUniforms) == 128);

// Данные одной модели (set 1, binding 0)
struct ModelUniforms {
	glm::mat4 model;
	glm::mat4 normal_matrix; // transpose(inverse(model)) — переводит нормали в мир с учётом масштаба
	glm::vec4 albedo;        // rgb — диффузный цвет материала, w не используется
	glm::vec4 specular;      // rgb — цвет блика, w — коэффициент блеска (shininess)
};
static_assert(sizeof(ModelUniforms) == 160);

} // namespace shader

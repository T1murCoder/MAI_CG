#pragma once

#include <glm/glm.hpp>

// Структуры, которые копируются в буферы GPU. Это контракт с шейдерами: раскладка каждой структуры
// должна совпадать с одноимённым блоком в shaders/common.glsl (uniform-блоки),
// shaders/lighting.glsl (элементы массивов источников света) или shaders/gizmo.glsl (push constant).
// Uniform-буферы используют раскладку std140, storage-буферы — std430: в обеих vec3 выровнен
// по 16 байтам, а в C++ занимает 12, поэтому в структурах только vec4/uvec4/mat4;
// static_assert ловит расхождение размеров при компиляции
namespace shader {

// Общие для всех моделей данные сцены (set 0, binding 0)
struct SceneUniforms {
	glm::mat4 view_projection;
	glm::vec4 camera_position; // xyz — положение камеры в мире (нужно для блика), w не используется
	glm::vec4 ambient;         // rgb — цвет рассеянного света, w — интенсивность
	glm::vec4 light_direction; // xyz — нормализованное направление лучей направленного света, w — 1/0 вкл/выкл
	glm::vec4 light_color;     // rgb — цвет направленного света, w — интенсивность
	glm::uvec4 light_counts;   // x — число точечных источников, y — прожекторов (в storage-буферах), zw не используются
};
static_assert(sizeof(SceneUniforms) == 144);

// Данные одной модели (set 1, binding 0)
struct ModelUniforms {
	glm::mat4 model;
	glm::mat4 normal_matrix; // transpose(inverse(model)) — переводит нормали в мир с учётом масштаба
	glm::vec4 albedo;        // rgb — диффузный цвет материала, w не используется
	glm::vec4 specular;      // rgb — цвет блика, w — коэффициент блеска (shininess)
};
static_assert(sizeof(ModelUniforms) == 160);

// Элемент массива точечных источников (set 0, binding 1, storage-буфер)
struct PointLight {
	glm::vec4 position; // xyz — положение в мире, w не используется
	glm::vec4 color;    // rgb — цвет, w — интенсивность
};
static_assert(sizeof(PointLight) == 32);

// Элемент массива прожекторов (set 0, binding 2, storage-буфер)
struct SpotLight {
	glm::vec4 position;  // xyz — положение в мире, w не используется
	glm::vec4 direction; // xyz — нормализованное направление оси конуса, w не используется
	glm::vec4 color;     // rgb — цвет, w — интенсивность
	glm::vec4 cutoff;    // x — cos(внутреннего угла), y — cos(внешнего угла), zw не используются
};
static_assert(sizeof(SpotLight) == 64);

// Push constant неосвещаемых фигур-визуализаций источников света (shaders/gizmo.glsl)
struct GizmoPushConstants {
	glm::mat4 model;
	glm::vec4 color; // rgb — постоянный цвет фигуры, w не используется
};
static_assert(sizeof(GizmoPushConstants) == 80);

} // namespace shader

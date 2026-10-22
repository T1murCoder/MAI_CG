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
};
static_assert(sizeof(SceneUniforms) == 64);

// Данные одной модели (set 1, binding 0)
struct ModelUniforms {
	glm::mat4 model;
	glm::vec4 color;
};
static_assert(sizeof(ModelUniforms) == 80);

} // namespace shader

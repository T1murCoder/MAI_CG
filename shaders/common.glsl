#ifndef COMMON_GLSL
#define COMMON_GLSL

// Блоки данных, общие для всех шейдеров. Это контракт с C++: раскладка каждого блока
// должна совпадать с одноимённой структурой в source/shader_types.hpp

// Общие для всех моделей данные сцены (set 0): матрица вида-проекции камеры
layout(set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
} scene;

// Данные текущей модели (set 1, свой набор на каждую модель): матрица модели и цвет
layout(set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	vec4 color;
} object;

#endif // COMMON_GLSL

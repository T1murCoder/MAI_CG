#ifndef COMMON_GLSL
#define COMMON_GLSL

// Uniform-блоки, общие для всех шейдеров. Это контракт с C++: раскладка каждого блока
// должна совпадать с одноимённой структурой в source/shader_types.hpp.
// Массивы источников света и модель освещения — в lighting.glsl (только для фрагментных шейдеров)

// Общие для всех моделей данные сцены (set 0): камера, рассеянный и направленный свет, число источников,
// тон-маппинг, тени направленного света
layout(set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec4 camera_position; // xyz — положение камеры в мире
	vec4 ambient;         // rgb — цвет, w — интенсивность
	vec4 light_direction; // xyz — нормализованное направление лучей, w — 1/0 вкл/выкл
	vec4 light_color;     // rgb — цвет, w — интенсивность
	uvec4 light_counts;   // x — число точечных источников, y — прожекторов
	vec4 tone_mapping;    // x — экспозиция, y — 1/0 тон-маппинг вкл/выкл
	mat4 light_view_projection; // мир → clip space направленного света
	vec4 shadow_params;   // x — сдвиг к источнику, y — сдвиг вдоль нормали (в текселях карты), z — радиус PCF,
						  // w — 1/0 тени вкл/выкл
	uvec4 shadow_debug;   // x — режим отладки теней (0 — выкл, 1 — направленный, 2 — прожектор, 3 — точечный),
						  // y — индекс источника в его массиве
} scene;

// Данные текущей модели (set 1, свой набор на каждую модель): трансформация и материал
layout(set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix; // transpose(inverse(model))
	vec4 albedo;        // rgb — диффузный цвет
	vec4 specular;      // rgb — цвет блика, w — коэффициент блеска
} object;

#endif // COMMON_GLSL

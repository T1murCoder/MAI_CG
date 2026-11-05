#ifndef COMMON_GLSL
#define COMMON_GLSL

// Uniform-блоки, общие для всех шейдеров. Это контракт с C++: раскладка каждого блока
// должна совпадать с одноимённой структурой в source/shader_types.hpp.
// Массивы источников света и модель освещения — в lighting.glsl (только для фрагментных шейдеров)

// Общие для всех моделей данные сцены (set 0): камера, рассеянный и направленный свет, число источников,
// тон-маппинг
layout(set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec4 camera_position; // xyz — положение камеры в мире
	vec4 ambient;         // rgb — цвет, w — интенсивность
	vec4 light_direction; // xyz — нормализованное направление лучей, w — 1/0 вкл/выкл
	vec4 light_color;     // rgb — цвет, w — интенсивность
	uvec4 light_counts;   // x — число точечных источников, y — прожекторов
	vec4 tone_mapping;    // x — экспозиция, y — тон-маппинг, z — ручное sRGB-кодирование для UNORM swapchain
} scene;

// Данные текущей модели (set 1, общий dynamic UBO): запись выбирается dynamic offset при draw
layout(set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix; // transpose(inverse(model))
	vec4 albedo;        // rgb — линейный множитель albedo-текстуры
	vec4 specular;      // rgb — цвет блика, w — коэффициент блеска
	vec4 uv_repeat;     // xy — повторение по U/V для всех карт материала
} object;

// Для sRGB swapchain цвет остаётся линейным: кодирование выполняет attachment.
// Для UNORM fallback кодируем один раз в шейдере, после освещения и тон-маппинга.
vec3 encodeOutputColor(vec3 linearColor) {
	if (scene.tone_mapping.z < 0.5) {
		return linearColor;
	}
	return mix(12.92 * linearColor,
		1.055 * pow(max(linearColor, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055,
		greaterThan(linearColor, vec3(0.0031308)));
}

#endif // COMMON_GLSL

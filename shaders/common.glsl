#ifndef COMMON_GLSL
#define COMMON_GLSL

// Блоки данных, общие для всех шейдеров. Это контракт с C++: раскладка каждого блока
// должна совпадать с одноимённой структурой в source/shader_types.hpp

// Общие для всех моделей данные сцены (set 0): камера, рассеянный и направленный свет
layout(set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec4 camera_position; // xyz — положение камеры в мире
	vec4 ambient;         // rgb — цвет, w — интенсивность
	vec4 light_direction; // xyz — нормализованное направление лучей, w — 1/0 вкл/выкл
	vec4 light_color;     // rgb — цвет, w — интенсивность
} scene;

// Данные текущей модели (set 1, свой набор на каждую модель): трансформация и материал
layout(set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix; // transpose(inverse(model))
	vec4 albedo;        // rgb — диффузный цвет
	vec4 specular;      // rgb — цвет блика, w — коэффициент блеска
} object;

// Вклад одного источника по модели Блинн-Фонга: (Kd·cosθ + Ks·cos^α φ)·I, где cosθ = N·L, cosφ = N·H.
// N, L, V — единичные векторы: нормаль, направление на источник, направление на камеру;
// radiance — цвет источника, уже умноженный на интенсивность и затухание.
// Обратная от источника сторона не получает ни диффузного света, ни блика
vec3 blinnPhong(vec3 N, vec3 L, vec3 V, vec3 radiance, vec3 albedo, vec3 specular, float shininess) {
	const float n_dot_l = dot(N, L);
	if (n_dot_l <= 0.0) {
		return vec3(0.0);
	}

	const vec3 H = normalize(L + V);
	const float n_dot_h = max(dot(N, H), 0.0);
	return (albedo * n_dot_l + specular * pow(n_dot_h, shininess)) * radiance;
}

#endif // COMMON_GLSL

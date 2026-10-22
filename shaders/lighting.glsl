#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

// Массивы источников света и модель освещения Блинн-Фонга. Подключается только во фрагментные
// шейдеры: storage-буферы источников видны в layout набора сцены только стадии FRAGMENT.
// Это контракт с C++: раскладка структур должна совпадать с source/shader_types.hpp

#include "common.glsl"

// Элемент массива точечных источников
struct PointLight {
	vec4 position; // xyz — положение в мире
	vec4 color;    // rgb — цвет, w — интенсивность
};

// Массив точечных источников (set 0, binding 1): длина массива в шейдере не задана,
// фактическое число элементов — scene.light_counts.x
layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight point_lights[];
};

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

#endif // LIGHTING_GLSL

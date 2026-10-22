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

// Элемент массива прожекторов
struct SpotLight {
	vec4 position;  // xyz — положение в мире
	vec4 direction; // xyz — нормализованное направление оси конуса
	vec4 color;     // rgb — цвет, w — интенсивность
	vec4 cutoff;    // x — cos(внутреннего угла), y — cos(внешнего угла)
};

// Массив прожекторов (set 0, binding 2): фактическое число элементов — scene.light_counts.y
layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spot_lights[];
};

// Для источника в точке light_position записывает в L единичное направление от точки x на источник
// и возвращает затухание по закону обратных квадратов 1/d². Квадрат расстояния ограничен снизу,
// чтобы вплотную к источнику не делить на ноль
float inverseSquareFalloff(vec3 light_position, vec3 x, out vec3 L) {
	const vec3 to_light = light_position - x;
	const float distance_squared = max(dot(to_light, to_light), 1e-4);
	L = to_light * inversesqrt(distance_squared);
	return 1.0 / distance_squared;
}

// Мягкий край прожектора: 1 внутри внутреннего конуса, 0 снаружи внешнего и линейный переход между ними
// по косинусу угла θ между осью конуса и лучом к точке. При равных углах знаменатель ограничен снизу,
// и переход вырождается в резкую границу
float spotFactor(vec3 L, vec3 spot_direction, float cos_inner, float cos_outer) {
	const float cos_theta = dot(-L, spot_direction);
	return clamp((cos_theta - cos_outer) / max(cos_inner - cos_outer, 1e-4), 0.0, 1.0);
}

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

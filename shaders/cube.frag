#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPosition;

layout(location = 0) out vec4 outColor;

// Освещённость пикселя: рассеянный свет + направленный и точечные источники по модели Блинн-Фонга
void main() {
	// После интерполяции нормаль перестаёт быть единичной — нормализуем заново
	const vec3 N = normalize(fragNormal);
	const vec3 V = normalize(scene.camera_position.xyz - fragWorldPosition);

	// Процедурный цвет вершин тонирует альбедо материала: при белом альбедо куб выглядит как в ЛР1
	const vec3 albedo = object.albedo.rgb * fragColor;
	const vec3 specular = object.specular.rgb;
	const float shininess = object.specular.w;

	vec3 color = scene.ambient.rgb * scene.ambient.w * albedo;

	// light_direction — куда летят лучи, а для модели нужно направление от точки к источнику
	if (scene.light_direction.w > 0.0) {
		const vec3 radiance = scene.light_color.rgb * scene.light_color.w;
		color += blinnPhong(N, -scene.light_direction.xyz, V, radiance, albedo, specular, shininess);
	}

	// Точечные источники: затухание по закону обратных квадратов 1/d²; квадрат расстояния ограничен
	// снизу, чтобы вплотную к источнику не делить на ноль
	for (uint i = 0; i < scene.light_counts.x; ++i) {
		const PointLight light = point_lights[i];
		const vec3 to_light = light.position.xyz - fragWorldPosition;
		const float distance_squared = max(dot(to_light, to_light), 1e-4);

		const vec3 L = to_light * inversesqrt(distance_squared);
		const vec3 radiance = light.color.rgb * light.color.w / distance_squared;
		color += blinnPhong(N, L, V, radiance, albedo, specular, shininess);
	}

	outColor = vec4(color, 1.0);
}

#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPosition;

layout(location = 0) out vec4 outColor;

// Освещённость пикселя: рассеянный свет + направленный источник по модели Блинн-Фонга
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

	outColor = vec4(color, 1.0);
}

#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPosition;

layout(location = 0) out vec4 outColor;

// Множитель тени источника, выбранного в режиме отладки теней (scene.shadow_debug): 1 — свет доходит,
// 0 — тень. Считается независимо от того, включён ли источник и попадает ли точка в конус прожектора,
// чтобы было видно саму карту теней. Источник за пределами массива не затеняет
float debugShadow(vec3 N) {
	const uint mode = scene.shadow_debug.x;
	const uint index = scene.shadow_debug.y;

	if (mode == 1u) {
		return directionalShadow(fragWorldPosition, N, -scene.light_direction.xyz);
	}
	if (mode == 2u && index < scene.light_counts.y) {
		const SpotLight light = spot_lights[index];
		return spotShadow(fragWorldPosition, N, normalize(light.position.xyz - fragWorldPosition), light);
	}
	if (mode == 3u && index < scene.light_counts.x) {
		const PointLight light = point_lights[index];
		return pointShadow(fragWorldPosition, N, normalize(light.position.xyz - fragWorldPosition), light);
	}
	return 1.0;
}

// Освещённость пикселя: рассеянный свет + направленный, точечные источники и прожекторы (все с тенями)
// по Блинн-Фонгу. В режиме отладки теней — только множитель тени выбранного источника
void main() {
	// После интерполяции нормаль перестаёт быть единичной — нормализуем заново
	const vec3 N = normalize(fragNormal);
	const vec3 V = normalize(scene.camera_position.xyz - fragWorldPosition);

	// Без тон-маппинга и освещения: оттенок серого — ровно значение множителя
	if (scene.shadow_debug.x != 0u) {
		outColor = vec4(vec3(debugShadow(N)), 1.0);
		return;
	}

	// Процедурный цвет вершин тонирует альбедо материала: при белом альбедо куб выглядит как в ЛР1
	const vec3 albedo = object.albedo.rgb * fragColor;
	const vec3 specular = object.specular.rgb;
	const float shininess = object.specular.w;

	vec3 color = scene.ambient.rgb * scene.ambient.w * albedo;

	// light_direction — куда летят лучи, а для модели нужно направление от точки к источнику.
	// Тень гасит вклад источника целиком — и диффузную часть, и блик
	if (scene.light_direction.w > 0.0) {
		const vec3 L = -scene.light_direction.xyz;
		const float shadow = directionalShadow(fragWorldPosition, N, L);
		const vec3 radiance = scene.light_color.rgb * scene.light_color.w * shadow;
		color += blinnPhong(N, L, V, radiance, albedo, specular, shininess);
	}

	// Точечные источники: затухание по закону обратных квадратов и тень
	for (uint i = 0; i < scene.light_counts.x; ++i) {
		const PointLight light = point_lights[i];

		vec3 L;
		const float falloff = inverseSquareFalloff(light.position.xyz, fragWorldPosition, L);
		const float shadow = pointShadow(fragWorldPosition, N, L, light);
		const vec3 radiance = light.color.rgb * light.color.w * falloff * shadow;
		color += blinnPhong(N, L, V, radiance, albedo, specular, shininess);
	}

	// Прожекторы: то же затухание, умноженное на мягкий край конуса и на тень
	for (uint i = 0; i < scene.light_counts.y; ++i) {
		const SpotLight light = spot_lights[i];

		vec3 L;
		const float falloff = inverseSquareFalloff(light.position.xyz, fragWorldPosition, L);
		const float spot = spotFactor(L, light.direction.xyz, light.cutoff.x, light.cutoff.y);
		const float shadow = spotShadow(fragWorldPosition, N, L, light);
		const vec3 radiance = light.color.rgb * light.color.w * falloff * spot * shadow;
		color += blinnPhong(N, L, V, radiance, albedo, specular, shininess);
	}

	// Тон-маппинг: экспозиция и оператор Рейнхарда x / (1 + x) плавно сжимают освещённость [0, ∞)
	// в [0, 1) вместо обрезки по 1 — яркий цветной свет не «выгорает» в белый. Гамма-коррекцию
	// после этого делает sRGB-формат swapchain
	if (scene.tone_mapping.y > 0.0) {
		color *= scene.tone_mapping.x;
		color = color / (1.0 + color);
	}

	outColor = vec4(color, 1.0);
}

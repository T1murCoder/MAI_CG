#version 450
#extension GL_GOOGLE_include_directive : require

#include "lighting.glsl"

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPosition;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform sampler2D albedoTexture;
layout(set = 1, binding = 2) uniform sampler2D specularTexture;
layout(set = 1, binding = 3) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 3) uniform sampler2DShadow sunShadowMap;

// Контракт с shader::AlbedoEffectPushConstants: первые 16 байт общего диапазона gizmos.
layout(push_constant) uniform AlbedoEffectPushConstants {
	vec4 animation; // x — время [0, 120), y — включение эффекта
} effect;

float sunVisibility(vec3 world_position) {
	if (scene.shadow_settings.x == 0u) {
		return 1.0;
	}
	const vec4 clip = scene.sun_view_projection * vec4(world_position, 1.0);
	if (clip.w <= 0.0) {
		return 1.0;
	}
	const vec3 projected = clip.xyz / clip.w;
	const vec2 uv = projected.xy * 0.5 + 0.5;
	// В Vulkan Z уже в [0, 1]. Инверсия Y включена в матрицу проекции света.
	// Объекты вне объёма карты не затеняются этой картой.
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))) ||
		projected.z < 0.0 || projected.z >= 1.0) {
		return 1.0;
	}
	// Один mip: явный LOD не требует производных в ветке проверки границ.
	// Comparison sampler возвращает 1, когда reference < сохранённой глубины.
	return textureLod(sunShadowMap, vec3(uv, projected.z), 0.0);
}

vec3 sampleAlbedo(vec2 uv) {
	if (effect.animation.y < 0.5) {
		return texture(albedoTexture, uv).rgb;
	}

	const float time = effect.animation.x;
	const float phase = time * (6.28318530718 / 120.0);
	// Две выборки одного изображения: разные масштабы и направления движения.
	// За 120 секунд смещения проходят целое число повторов, поэтому сброс часов незаметен.
	const vec3 first = texture(albedoTexture, uv + time * vec2(0.05, 0.025)).rgb;
	const vec3 second = texture(albedoTexture, uv * 1.5 + time * vec2(-0.025, 0.05)).rgb;
	const float blend = 0.5 + 0.2 * sin(3.0 * phase); // обе выборки всегда участвуют: 30…70 %
	const vec3 modulation = 0.75 + 0.25 * sin(vec3(4.0 * phase) + vec3(0.0, 2.0943951, 4.1887902));
	return mix(first, second, blend) * modulation;
}

// Освещённость пикселя: рассеянный свет + направленный, точечные источники и прожекторы по Блинн-Фонгу
void main() {
	// После интерполяции нормаль перестаёт быть единичной — нормализуем заново
	const vec3 N = normalize(fragNormal);
	const vec3 V = normalize(scene.camera_position.xyz - fragWorldPosition);

	// sRGB-текстура декодируется аппаратно; освещение и tint применяются в линейном пространстве.
	// Общие UV для albedo/specular/emissive. Повторение выполняет repeat-сэмплер:
	// сохраняем непрерывные координаты и производные для линейной фильтрации.
	const vec2 materialUv = fragUv * object.uv_repeat.xy;
	const vec3 albedo = object.albedo.rgb * sampleAlbedo(materialUv);
	// Specular — линейные данные (UNORM), с теми же UV, что и albedo.
	// Чёрный fallback или нулевой множитель модели полностью убирают блики.
	const vec3 specular = object.specular.rgb * texture(specularTexture, materialUv).rgb;
	const float shininess = object.specular.w;

	const bool sun_only = scene.shadow_settings.y != 0u;
	// Изоляция влияет только на вычисление освещения; CPU-настройки источников сохраняются.
	const uvec2 local_light_counts = sun_only ? uvec2(0u) : scene.light_counts.xy;
	vec3 color = sun_only ? vec3(0.0) : scene.ambient.rgb * scene.ambient.w * albedo;

	// light_direction — куда летят лучи, а для модели нужно направление от точки к источнику
	if (scene.light_direction.w > 0.0) {
		const vec3 radiance = scene.light_color.rgb * scene.light_color.w;
		const float visibility = sunVisibility(fragWorldPosition);
		color += visibility * blinnPhong(N, -scene.light_direction.xyz, V, radiance, albedo, specular, shininess);
	}

	// Точечные источники: затухание по закону обратных квадратов
	for (uint i = 0; i < local_light_counts.x; ++i) {
		const PointLight light = point_lights[i];

		vec3 L;
		const float falloff = inverseSquareFalloff(light.position.xyz, fragWorldPosition, L);
		const vec3 radiance = light.color.rgb * light.color.w * falloff;
		color += blinnPhong(N, L, V, radiance, albedo, specular, shininess);
	}

	// Прожекторы: то же затухание, умноженное на мягкий край конуса
	for (uint i = 0; i < local_light_counts.y; ++i) {
		const SpotLight light = spot_lights[i];

		vec3 L;
		const float falloff = inverseSquareFalloff(light.position.xyz, fragWorldPosition, L);
		const float spot = spotFactor(L, light.direction.xyz, light.cutoff.x, light.cutoff.y);
		const vec3 radiance = light.color.rgb * light.color.w * falloff * spot;
		color += blinnPhong(N, L, V, radiance, albedo, specular, shininess);
	}

	// Emissive декодируется из sRGB и прибавляется в линейном пространстве после
	// освещения. Не зависит от внешнего света и albedo tint; UV общие со всеми картами.
	color += texture(emissiveTexture, materialUv).rgb * object.emissive.x;

	// Тон-маппинг: экспозиция и оператор Рейнхарда x / (1 + x) плавно сжимают освещённость [0, ∞)
	// в [0, 1) вместо обрезки по 1 — яркий цветной свет не «выгорает» в белый. Гамма-коррекцию
	// после этого делает sRGB-формат swapchain либо encodeOutputColor для UNORM
	if (scene.tone_mapping.y > 0.0) {
		color *= scene.tone_mapping.x;
		color = color / (1.0 + color);
	}

	outColor = vec4(encodeOutputColor(color), 1.0);
}

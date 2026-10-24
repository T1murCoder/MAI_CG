#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

// Массивы источников света, выборка из карт теней и модель освещения Блинн-Фонга. Подключается только
// во фрагментные шейдеры: storage-буферы источников и карты теней видны в layout набора сцены только
// стадии FRAGMENT.
// Это контракт с C++: раскладка структур должна совпадать с source/shader_types.hpp

#include "common.glsl"

// Элемент массива точечных источников
struct PointLight {
	vec4 position; // xyz — положение в мире
	vec4 color;    // rgb — цвет, w — интенсивность
	vec4 shadow;   // x — номер кубической карты теней или −1 (без тени), y — дальность карты (far)
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
	vec4 cutoff;    // x — cos(внутреннего угла), y — cos(внешнего угла), z — слой карты теней или −1 (без тени),
					// w — tan половины угла обзора карты теней
	mat4 view_projection; // мир → clip space прожектора
};

// Массив прожекторов (set 0, binding 2): фактическое число элементов — scene.light_counts.y
layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spot_lights[];
};

// Карты теней. Сэмплер со сравнением: texture() возвращает не глубину, а долю освещённости — 1, если точка
// не дальше от источника, чем глубина в карте, иначе 0 (с линейным фильтром — среднее по 4 соседним текселям).
// Направленный свет — одна карта (set 0, binding 3), прожекторы — массив карт, слой на прожектор (binding 4),
// точечные источники — массив кубических карт, куб на источник (binding 5). В кубические карты записано
// не глубина, а расстояние до источника, делённое на дальность карты
layout(set = 0, binding = 3) uniform sampler2DShadow directional_shadow_map;
layout(set = 0, binding = 4) uniform sampler2DArrayShadow spot_shadow_maps;
layout(set = 0, binding = 5) uniform samplerCubeArrayShadow point_shadow_maps;

// Сдвиги против «акне» (самозатенения поверхности из-за конечного разрешения карты), в текселях карты:
// texel_world_size — размер текселя в мире у этой точки. Точка сдвигается к источнику (вдоль L — по тому же
// лучу, поэтому попадает в тот же тексель, но становится ближе к источнику) и вдоль нормали N — сильнее
// при скользящем свете, где глубина поверхности быстрее всего меняется в пределах текселя.
// Сдвиг в мировых единицах одинаково работает в орто- и перспективной проекции, в отличие от сдвига
// нелинейной глубины перспективы
vec3 shadowBiasedPosition(vec3 world_position, vec3 N, vec3 L, float texel_world_size) {
	const float n_dot_l = clamp(dot(N, L), 0.0, 1.0);
	return world_position +
		   texel_world_size * (L * scene.shadow_params.x + N * scene.shadow_params.y * (1.0 - n_dot_l));
}

// Перевод clip-координат источника в координаты карты: xy из [-1, 1] в [0, 1], глубина уже в [0, 1]
vec3 shadowMapCoordinates(vec4 light_clip) {
	const vec3 ndc = light_clip.xyz / light_clip.w;
	return vec3(ndc.xy * 0.5 + 0.5, ndc.z);
}

// PCF: среднее результатов сравнения по квадрату (2r + 1)² соседних текселей — граница тени
// размывается на ширину ядра вместо «лесенки» из текселей карты. r = 0 — одна выборка.
// Выход uv за [0, 1] отдельно не проверяется: там сэмплер возвращает белую границу (глубина 1) — «освещено»
float directionalPcf(vec3 map_coordinates) {
	const int radius = int(scene.shadow_params.z);
	const vec2 texel = 1.0 / vec2(textureSize(directional_shadow_map, 0));

	float lit = 0.0;
	for (int y = -radius; y <= radius; ++y) {
		for (int x = -radius; x <= radius; ++x) {
			lit += texture(directional_shadow_map, vec3(map_coordinates.xy + vec2(x, y) * texel, map_coordinates.z));
		}
	}

	const float side = float(2 * radius + 1);
	return lit / (side * side);
}

// То же для слоя layer массива карт прожекторов
float spotPcf(vec3 map_coordinates, float layer) {
	const int radius = int(scene.shadow_params.z);
	const vec2 texel = 1.0 / vec2(textureSize(spot_shadow_maps, 0).xy);

	float lit = 0.0;
	for (int y = -radius; y <= radius; ++y) {
		for (int x = -radius; x <= radius; ++x) {
			const vec2 uv = map_coordinates.xy + vec2(x, y) * texel;
			lit += texture(spot_shadow_maps, vec4(uv, layer, map_coordinates.z));
		}
	}

	const float side = float(2 * radius + 1);
	return lit / (side * side);
}

// Освещённость точки направленным светом с учётом теней: 1 — свет доходит, 0 — точка в тени.
// N и L — единичные нормаль и направление на источник
float directionalShadow(vec3 world_position, vec3 N, vec3 L) {
	if (scene.shadow_params.w == 0.0) {
		return 1.0;
	}

	// Тексель ортопроекции одинаков по всей карте: строка 0 матрицы вида-проекции — ось X вида,
	// умноженная на масштаб проекции 2 / ширина, поэтому её длина даёт ширину карты в мире
	const mat4 m = scene.light_view_projection;
	const float map_world_size = 2.0 / length(vec3(m[0][0], m[1][0], m[2][0]));
	const float texel_world_size = map_world_size / float(textureSize(directional_shadow_map, 0).x);

	const vec3 position = shadowBiasedPosition(world_position, N, L, texel_world_size);
	const vec3 map_coordinates = shadowMapCoordinates(m * vec4(position, 1.0));

	// Дальше дальней плоскости карты — ничего не известно, считаем освещённым
	if (map_coordinates.z > 1.0) {
		return 1.0;
	}

	return directionalPcf(map_coordinates);
}

// Освещённость точки прожектором light с учётом теней. Прожекторы без слоя карты (cutoff.z < 0) не затеняются
float spotShadow(vec3 world_position, vec3 N, vec3 L, SpotLight light) {
	const float layer = light.cutoff.z;
	if (scene.shadow_params.w == 0.0 || layer < 0.0) {
		return 1.0;
	}

	// В перспективе тексель растёт с удалённостью: w в clip space — расстояние вдоль оси прожектора,
	// на нём карта покрывает в мире 2·w·tan(fov/2)
	const float w = (light.view_projection * vec4(world_position, 1.0)).w;
	const float texel_world_size = 2.0 * w * light.cutoff.w / float(textureSize(spot_shadow_maps, 0).x);

	const vec4 light_clip = light.view_projection * vec4(shadowBiasedPosition(world_position, N, L, texel_world_size), 1.0);

	// Позади прожектора (w <= 0) и дальше дальней плоскости — считаем освещённым; вне конуса точку
	// всё равно гасит spotFactor
	if (light_clip.w <= 0.0) {
		return 1.0;
	}

	const vec3 map_coordinates = shadowMapCoordinates(light_clip);
	if (map_coordinates.z > 1.0) {
		return 1.0;
	}

	return spotPcf(map_coordinates, layer);
}

// Смещения выборок PCF кубической карты: вершины куба и середины его рёбер — 20 направлений,
// равномерно покрывающих окрестность луча во всех трёх измерениях
const vec3 kCubePcfOffsets[20] = vec3[](
	vec3(1, 1, 1), vec3(1, -1, 1), vec3(-1, -1, 1), vec3(-1, 1, 1),
	vec3(1, 1, -1), vec3(1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
	vec3(1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
	vec3(1, 0, 1), vec3(-1, 0, 1), vec3(1, 0, -1), vec3(-1, 0, -1),
	vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, -1, -1), vec3(0, 1, -1)
);

// Освещённость точки точечным источником light с учётом теней. Источники без кубической карты
// (shadow.x < 0) не затеняются
float pointShadow(vec3 world_position, vec3 N, vec3 L, PointLight light) {
	const float index = light.shadow.x;
	const float map_far = light.shadow.y;
	if (scene.shadow_params.w == 0.0 || index < 0.0) {
		return 1.0;
	}

	// Грань куба — перспектива 90°: на расстоянии d вдоль оси грани она покрывает в мире 2d.
	// Ось грани — наибольшая по модулю координата вектора от источника к точке
	const vec3 to_point = world_position - light.position.xyz;
	const float axis_distance = max(max(abs(to_point.x), abs(to_point.y)), abs(to_point.z));
	const float texel_world_size = 2.0 * axis_distance / float(textureSize(point_shadow_maps, 0).x);

	// Вектор от источника к сдвинутой точке — и направление выборки из куба (нормализовать не нужно),
	// и расстояние для сравнения с записанным в карту
	const vec3 v = shadowBiasedPosition(world_position, N, L, texel_world_size) - light.position.xyz;
	const float reference = length(v) / map_far;

	// Дальше дальности карты — ничего не известно, считаем освещённым
	if (reference > 1.0) {
		return 1.0;
	}

	const int radius = int(scene.shadow_params.z);
	if (radius == 0) {
		return texture(point_shadow_maps, vec4(v, index), reference);
	}

	// PCF: выборки вокруг луча на расстоянии radius текселей. Соседние грани куба Vulkan фильтрует
	// бесшовно, поэтому выборки у ребра куба берутся с соседней грани без разрыва
	const float spread = float(radius) * texel_world_size;
	float lit = 0.0;
	for (int i = 0; i < 20; ++i) {
		lit += texture(point_shadow_maps, vec4(v + kCubePcfOffsets[i] * spread, index), reference);
	}
	return lit / 20.0;
}

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

// Вклад одного источника по модели Блинн-Фонга: (Kd + Ks·cos^α φ)·cosθ·I, где cosθ = N·L, cosφ = N·H.
// N, L, V — единичные векторы: нормаль, направление на источник, направление на камеру;
// radiance — цвет источника, уже умноженный на интенсивность и затухание.
// Блик тоже умножается на cosθ: при скользящем свете (N·L → 0) он плавно гаснет вместе
// с диффузной частью. Иначе блик обрывался бы ступенькой на N·L = 0, и погрешность
// интерполяции позиции превращала бы эту границу в шум. Обратная сторона не освещается
vec3 blinnPhong(vec3 N, vec3 L, vec3 V, vec3 radiance, vec3 albedo, vec3 specular, float shininess) {
	const float n_dot_l = max(dot(N, L), 0.0);
	const vec3 H = normalize(L + V);
	const float n_dot_h = max(dot(N, H), 0.0);
	return (albedo + specular * pow(n_dot_h, shininess)) * n_dot_l * radiance;
}

#endif // LIGHTING_GLSL

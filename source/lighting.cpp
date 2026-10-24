#include "lighting.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

namespace {

// Сфера, которую должна целиком покрывать карта теней направленного света: комната из Scene::createModels —
// пол 12×12 на y = −1, стены до y = 5. Радиус — половина диагонали коробки с полуразмерами (6, 3, 6)
const glm::vec3 kSceneCenter = glm::vec3(0.0f, 2.0f, 0.0f);
constexpr float kSceneRadius = 9.0f;

// Перспектива карты теней прожектора. Ближняя плоскость — не слишком близко, иначе точность
// нелинейной глубины уходит к самому источнику; дальняя — с запасом на диагональ комнаты (≈ 17).
// Запас угла нужен, чтобы ядро PCF у края конуса не выходило за карту; угол обзора перспективы
// должен быть меньше 180°, поэтому он ограничен сверху
constexpr float kSpotShadowNear = 0.1f;
constexpr float kSpotShadowFar = 25.0f;
constexpr float kSpotShadowMarginDegrees = 5.0f;
constexpr float kSpotShadowMaxFovDegrees = 170.0f;

// Угол обзора карты теней прожектора с внешним углом outer_degrees
float spotShadowFovDegrees(float outer_degrees) {
	return std::min(2.0f * outer_degrees + kSpotShadowMarginDegrees, kSpotShadowMaxFovDegrees);
}

// Список источников одного типа: кнопка добавления и по ветке дерева на каждый источник с кнопкой
// удаления. Удаление откладывается до конца цикла, чтобы не менять вектор во время обхода
template <typename Light>
void drawLightList(const char* title, const char* item_label, std::vector<Light>& lights) {
	ImGui::PushID(title);
	ImGui::SeparatorText(title);
	if (ImGui::Button("Add")) {
		lights.push_back(Light{});
	}

	int remove_index = -1;
	for (int i = 0; i < int(lights.size()); ++i) {
		ImGui::PushID(i);
		if (ImGui::TreeNodeEx("light", ImGuiTreeNodeFlags_DefaultOpen, "%s %d", item_label, i)) {
			lights[i].drawUi();
			if (ImGui::Button("Remove")) {
				remove_index = i;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	if (remove_index >= 0) {
		lights.erase(lights.begin() + remove_index);
	}
	ImGui::PopID();
}

} // namespace

glm::vec3 normalizeDirection(glm::vec3 direction) {
	return glm::dot(direction, direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
}

glm::vec3 upReference(glm::vec3 direction) {
	return std::abs(direction.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
}

void AmbientLight::drawUi() {
	ImGui::SeparatorText("Ambient Light");
	ImGui::ColorEdit3("Ambient Color", &color.x);
	ImGui::SliderFloat("Ambient Intensity", &intensity, 0.0f, 1.0f);
}

// «Камера» источника стоит на краю сферы сцены против лучей и смотрит в её центр; куб ортопроекции
// со стороной 2R вокруг центра покрывает всю сцену. Ось Y не переворачивается, как у камеры:
// карту рисуют и читают одной и той же матрицей, поэтому uv = ndc.xy · 0.5 + 0.5 верно и так.
// Глубина благодаря GLM_FORCE_DEPTH_ZERO_TO_ONE попадает в [0, 1]
glm::mat4 DirectionalLight::viewProjection() const {
	const glm::vec3 rays = normalizeDirection(direction);
	const glm::mat4 view = glm::lookAt(kSceneCenter - rays * kSceneRadius, kSceneCenter, upReference(rays));
	const glm::mat4 projection =
		glm::ortho(-kSceneRadius, kSceneRadius, -kSceneRadius, kSceneRadius, 0.0f, 2.0f * kSceneRadius);
	return projection * view;
}

// Направление задаётся вектором без нормализации — в шейдер уходит уже единичный
void DirectionalLight::drawUi() {
	ImGui::SeparatorText("Directional Light");
	ImGui::Checkbox("Enabled", &enabled);
	ImGui::Checkbox("Cast Shadows", &cast_shadows);
	ImGui::SliderFloat3("Direction", &direction.x, -1.0f, 1.0f);
	ImGui::ColorEdit3("Light Color", &color.x);
	ImGui::SliderFloat("Light Intensity", &intensity, 0.0f, 5.0f);
}

glm::vec3 PointLight::worldPosition() const {
	return animation.apply({ .position = position }).position;
}

// Из-за затухания 1/d² интенсивности нужен большой запас: на расстоянии 3 остаётся 1/9 света.
// У точечного источника нет ориентации, поэтому скорости вращения в UI анимации скрыты
void PointLight::drawUi() {
	ImGui::DragFloat3("Position", &position.x, 0.02f);
	ImGui::ColorEdit3("Color", &color.x);
	ImGui::SliderFloat("Intensity", &intensity, 0.0f, 50.0f);
	ImGui::Checkbox("Cast Shadows", &cast_shadows);
	animation.drawUi(false);
}

glm::vec3 SpotLight::worldPosition() const {
	return animation.apply({ .position = position }).position;
}

// Накопленное вращение анимации поворачивает ось конуса: например, вращение вокруг Y
// у наклонённого прожектора заставляет пятно ходить по кругу
glm::vec3 SpotLight::worldDirection() const {
	const Transform rotation = { .rotation_degrees = animation.apply({}).rotation_degrees };
	return normalizeDirection(glm::mat3(rotation.matrix()) * normalizeDirection(direction));
}

// «Камера» стоит в прожекторе и смотрит вдоль оси конуса; у прожектора, светящего вертикально
// (как стартовый), опорная ось «вверх» берётся из upReference, иначе lookAt вырождается.
// Как и у направленного света, Y не переворачивается: карту рисуют и читают одной матрицей
glm::mat4 SpotLight::viewProjection() const {
	const glm::vec3 eye = worldPosition();
	const glm::vec3 axis = worldDirection();
	const glm::mat4 view = glm::lookAt(eye, eye + axis, upReference(axis));

	const float fov = glm::radians(spotShadowFovDegrees(outer_degrees));
	const glm::mat4 projection = glm::perspective(fov, 1.0f, kSpotShadowNear, kSpotShadowFar);
	return projection * view;
}

// Углы связаны: увеличение inner подтягивает outer, уменьшение outer подтягивает inner,
// поэтому outer >= inner всегда и мягкий край не «выворачивается»
void SpotLight::drawUi() {
	ImGui::DragFloat3("Position", &position.x, 0.02f);
	ImGui::SliderFloat3("Direction", &direction.x, -1.0f, 1.0f);
	ImGui::ColorEdit3("Color", &color.x);
	ImGui::SliderFloat("Intensity", &intensity, 0.0f, 100.0f);
	if (ImGui::SliderFloat("Inner Angle", &inner_degrees, 1.0f, 89.0f)) {
		outer_degrees = std::max(outer_degrees, inner_degrees);
	}
	if (ImGui::SliderFloat("Outer Angle", &outer_degrees, 1.0f, 89.0f)) {
		inner_degrees = std::min(inner_degrees, outer_degrees);
	}
	ImGui::Checkbox("Cast Shadows", &cast_shadows);
	animation.drawUi();
}

bool Lighting::create(VkDescriptorSet scene_set) {
	return point_light_buffer.create(sizeof(shader::PointLight), scene_set, 1) &&
		   spot_light_buffer.create(sizeof(shader::SpotLight), scene_set, 2);
}

void Lighting::destroy() {
	spot_light_buffer.destroy();
	point_light_buffer.destroy();
}

void Lighting::update(double dt) {
	for (PointLight& light : point_lights) {
		light.animation.update(dt);
	}
	for (SpotLight& light : spot_lights) {
		light.animation.update(dt);
	}
}

// Источники упаковываются в формат GPU (vec3 → vec4, углы → косинусы); шейдеру сообщается,
// сколько источников реально загружено
void Lighting::upload(shader::SceneUniforms& uniforms) {
	uniforms.ambient = glm::vec4(ambient.color, ambient.intensity);
	uniforms.light_direction =
		glm::vec4(normalizeDirection(directional.direction), directional.enabled ? 1.0f : 0.0f);
	uniforms.light_color = glm::vec4(directional.color, directional.intensity);
	uniforms.light_view_projection = directional.viewProjection();

	// Карта направленного света одна: если он не отбрасывает тени, проход теней её только очищает,
	// и все точки остаются освещёнными
	directional_shadow.reset();
	if (directional.enabled && directional.cast_shadows) {
		directional_shadow = uniforms.light_view_projection;
	}

	// Кубические карты теней раздаются по порядку первым kMaxShadowedPointLights источникам с cast_shadows,
	// остальные получают −1. Положения источников с картами запоминаются для проходов теней в том же порядке
	point_shadows.clear();
	std::vector<shader::PointLight> gpu_point_lights;
	gpu_point_lights.reserve(point_lights.size());
	for (const PointLight& light : point_lights) {
		float shadow_index = -1.0f;
		if (light.cast_shadows && point_shadows.size() < kMaxShadowedPointLights) {
			shadow_index = float(point_shadows.size());
			point_shadows.push_back(light.worldPosition());
		}
		gpu_point_lights.push_back({
			.position = glm::vec4(light.worldPosition(), 1.0f),
			.color = glm::vec4(light.color, light.intensity),
			.shadow = glm::vec4(shadow_index, kPointShadowFar, 0.0f, 0.0f),
		});
	}

	// Слои карт прожекторов раздаются так же. Тангенс половины угла обзора карты нужен шейдеру,
	// чтобы знать размер текселя карты на расстоянии точки
	spot_shadows.clear();
	std::vector<shader::SpotLight> gpu_spot_lights;
	gpu_spot_lights.reserve(spot_lights.size());
	for (const SpotLight& light : spot_lights) {
		const glm::mat4 view_projection = light.viewProjection();
		float shadow_layer = -1.0f;
		if (light.cast_shadows && spot_shadows.size() < kMaxShadowedSpotLights) {
			shadow_layer = float(spot_shadows.size());
			spot_shadows.push_back(view_projection);
		}
		const float tan_half_fov = std::tan(glm::radians(spotShadowFovDegrees(light.outer_degrees)) * 0.5f);
		gpu_spot_lights.push_back({
			.position = glm::vec4(light.worldPosition(), 1.0f),
			.direction = glm::vec4(light.worldDirection(), 0.0f),
			.color = glm::vec4(light.color, light.intensity),
			.cutoff = glm::vec4(std::cos(glm::radians(light.inner_degrees)),
								std::cos(glm::radians(light.outer_degrees)), shadow_layer, tan_half_fov),
			.view_projection = view_projection,
		});
	}

	uniforms.light_counts.x =
		point_light_buffer.upload(gpu_point_lights.data(), uint32_t(gpu_point_lights.size()));
	uniforms.light_counts.y =
		spot_light_buffer.upload(gpu_spot_lights.data(), uint32_t(gpu_spot_lights.size()));
}

void Lighting::drawUi() {
	ambient.drawUi();
	directional.drawUi();
	drawLightList("Point Lights", "Point Light", point_lights);
	drawLightList("Spot Lights", "Spot Light", spot_lights);
}

const std::optional<glm::mat4>& Lighting::directionalShadowViewProjection() const {
	return directional_shadow;
}

const std::vector<glm::mat4>& Lighting::spotShadowViewProjections() const {
	return spot_shadows;
}

const std::vector<glm::vec3>& Lighting::pointShadowPositions() const {
	return point_shadows;
}

const std::vector<PointLight>& Lighting::pointLights() const {
	return point_lights;
}

const std::vector<SpotLight>& Lighting::spotLights() const {
	return spot_lights;
}

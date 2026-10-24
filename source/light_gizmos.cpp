#include "light_gizmos.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include "graphics_internal.hpp"
#include "pipeline.hpp"

namespace {

constexpr float kMarkerSize = 0.15f;     // ребро куба-маркера источника
constexpr float kConeLength = 1.0f;      // длина каркасного конуса прожектора вдоль оси
constexpr uint32_t kConeSegments = 32;   // отрезков в окружности основания конуса

const glm::vec3 kOuterConeColor = glm::vec3(1.0f);
const glm::vec3 kInnerConeColor = glm::vec3(0.45f);

// Поворот, переводящий ось +Z в direction (единичный вектор); базис достраивается от опорной оси upReference
glm::mat4 orientationAlong(glm::vec3 direction) {
	const glm::vec3 x = glm::normalize(glm::cross(upReference(direction), direction));
	const glm::vec3 y = glm::cross(direction, x);
	return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(direction, 0.0f),
					 glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

// Матрица единичного конуса (вершина в начале координат, ось +Z, радиус основания 1 при z = 1):
// вершина — в положении прожектора, ось — по его направлению, радиус основания — под угол раствора
glm::mat4 coneMatrix(const SpotLight& light, float half_angle_degrees) {
	const float radius = kConeLength * std::tan(glm::radians(half_angle_degrees));
	return glm::translate(glm::mat4(1.0f), light.worldPosition()) * orientationAlong(light.worldDirection()) *
		   glm::scale(glm::mat4(1.0f), glm::vec3(radius, radius, kConeLength));
}

glm::mat4 markerMatrix(glm::vec3 position) {
	return glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(kMarkerSize));
}

} // namespace

bool LightGizmos::create(VkPipelineLayout layout, const Mesh* marker_mesh) {
	pipeline_layout = layout;
	marker = marker_mesh;

	return cone_mesh.create(geometry::makeConeLines(kConeSegments)) && createPipelines();
}

void LightGizmos::destroy() {
	destroyPipelines();
	cone_mesh.destroy();
}

bool LightGizmos::createPipelines() {
	if (!pipeline::create({
			.vertex_shader = "unlit.vert.spv",
			.fragment_shader = "unlit.frag.spv",
			.layout = pipeline_layout,
		}, marker_pipeline)) {
		return false;
	}

	// У линий нет лицевой и обратной стороны — отсечение отключаем
	return pipeline::create({
		.vertex_shader = "unlit.vert.spv",
		.fragment_shader = "unlit.frag.spv",
		.layout = pipeline_layout,
		.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		.cull_mode = VK_CULL_MODE_NONE,
	}, line_pipeline);
}

void LightGizmos::destroyPipelines() {
	auto& context = graphics::internal::context;

	vkDestroyPipeline(context.device, line_pipeline, nullptr);
	vkDestroyPipeline(context.device, marker_pipeline, nullptr);
	line_pipeline = VK_NULL_HANDLE;
	marker_pipeline = VK_NULL_HANDLE;
}

void LightGizmos::record(VkCommandBuffer command_buffer, const Lighting& lighting) const {
	if (!visible) {
		return;
	}

	// Кубы-маркеры цвета источника у точечных источников и прожекторов
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, marker_pipeline);
	marker->bind(command_buffer);
	for (const PointLight& light : lighting.pointLights()) {
		drawMesh(command_buffer, *marker, markerMatrix(light.worldPosition()), light.color);
	}
	for (const SpotLight& light : lighting.spotLights()) {
		drawMesh(command_buffer, *marker, markerMatrix(light.worldPosition()), light.color);
	}

	// Каркасные конусы прожекторов: внешний угол — белый, внутренний (граница мягкого края) — серый
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline);
	cone_mesh.bind(command_buffer);
	for (const SpotLight& light : lighting.spotLights()) {
		drawMesh(command_buffer, cone_mesh, coneMatrix(light, light.outer_degrees), kOuterConeColor);
		drawMesh(command_buffer, cone_mesh, coneMatrix(light, light.inner_degrees), kInnerConeColor);
	}
}

void LightGizmos::drawUi() {
	ImGui::Checkbox("Show Light Gizmos", &visible);
}

// Меш уже привязан вызывающим кодом — для серии фигур с одним мешем он привязывается один раз
void LightGizmos::drawMesh(VkCommandBuffer command_buffer, const Mesh& mesh, const glm::mat4& model,
						   glm::vec3 color) const {
	const shader::GizmoPushConstants push_constants = {
		.model = model,
		.color = glm::vec4(color, 1.0f),
	};
	vkCmdPushConstants(command_buffer, pipeline_layout, kPushConstantRange.stageFlags, 0, sizeof(push_constants),
					   &push_constants);
	mesh.draw(command_buffer);
}

#pragma once

#include <vulkan/vulkan_core.h>

#include "lighting.hpp"
#include "mesh.hpp"
#include "shader_types.hpp"

// Визуализация источников света: маленький куб цвета источника у каждого точечного источника
// и прожектора, плюс каркасные конусы прожекторов (внешний — белый, внутренний — серый).
// Фигуры рисуются отдельными пайплайнами с постоянным цветом и не освещаются
class LightGizmos {
public:
	// Push constant фигур; pipeline layout сцены должен содержать этот диапазон
	static constexpr VkPushConstantRange kPushConstantRange = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(shader::GizmoPushConstants),
	};

	// Создаёт меш конуса и пайплайны фигур под pipeline layout сцены; marker_mesh — меш маркера
	// источника (не принадлежит визуализации и должен жить дольше неё)
	bool create(VkPipelineLayout layout, const Mesh* marker_mesh);
	void destroy();

	// Рисует фигуры всех источников, если визуализация включена. Набор сцены (set 0) с матрицей
	// вида-проекции уже должен быть привязан с тем же pipeline layout
	void record(VkCommandBuffer command_buffer, const Lighting& lighting) const;

	// Чекбокс показа/скрытия визуализации в текущем окне ImGui
	void drawUi();

private:
	// Передаёт фигуре её матрицу и цвет и рисует меш
	void drawMesh(VkCommandBuffer command_buffer, const Mesh& mesh, const glm::mat4& model,
				  glm::vec3 color) const;

	bool visible = true;

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE; // принадлежит сцене
	const Mesh* marker = nullptr;
	Mesh cone_mesh;

	VkPipeline marker_pipeline = VK_NULL_HANDLE; // треугольники
	VkPipeline line_pipeline = VK_NULL_HANDLE;   // линии конусов
};

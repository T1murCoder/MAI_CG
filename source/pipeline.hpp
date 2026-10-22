#pragma once

#include <span>

#include <vulkan/vulkan_core.h>

namespace pipeline {

// Параметры графического конвейера, которые различаются между пайплайнами сцены;
// формат вершины общий для всех — geometry::Vertex
struct Config {
	const char* vertex_shader;   // имя .spv-файла в SHADER_DIR
	const char* fragment_shader; // имя .spv-файла в SHADER_DIR
	VkPipelineLayout layout;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
	bool depth_write = true;
};

// Читает скомпилированный SPIR-V файл из SHADER_DIR и создаёт из него VkShaderModule
VkShaderModule loadShaderModule(const char* filename);

// Создаёт pipeline layout из наборов дескрипторов и диапазонов push constant
bool createLayout(std::span<const VkDescriptorSetLayout> set_layouts,
				  std::span<const VkPushConstantRange> push_constants, VkPipelineLayout& out);

// Создаёт graphics pipeline под render pass заготовки по параметрам config
bool create(const Config& config, VkPipeline& out);

} // namespace pipeline

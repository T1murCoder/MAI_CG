#pragma once

#include <span>

#include <vulkan/vulkan_core.h>

namespace pipeline {

enum class VertexInput { Mesh, PositionOnly, None };

// Формат вершин мешей — geometry::Vertex; fullscreen-проход генерирует вершины в шейдере.
struct Config {
	const char* vertex_shader;   // имя .spv-файла в SHADER_DIR
	const char* fragment_shader; // nullptr для depth-only pipeline
	VkPipelineLayout layout;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
	bool depth_write = true;
	bool depth_test = true;
	VertexInput vertex_input = VertexInput::Mesh;
	// UNDEFINED — основной render pass; иначе depth-only Dynamic Rendering с этим форматом.
	VkFormat dynamic_depth_format = VK_FORMAT_UNDEFINED;
	// При включении параметры задаются через vkCmdSetDepthBias перед отрисовкой.
	bool depth_bias = false;
};

// Читает скомпилированный SPIR-V файл из SHADER_DIR и создаёт из него VkShaderModule
VkShaderModule loadShaderModule(const char* filename);

// Создаёт pipeline layout из наборов дескрипторов и диапазонов push constant
bool createLayout(std::span<const VkDescriptorSetLayout> set_layouts,
				  std::span<const VkPushConstantRange> push_constants, VkPipelineLayout& out);

// Создаёт graphics pipeline под основной render pass или depth-only Dynamic Rendering.
bool create(const Config& config, VkPipeline& out);

} // namespace pipeline

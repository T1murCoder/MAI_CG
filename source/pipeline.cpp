#include "pipeline.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "geometry.hpp"
#include "graphics_internal.hpp"

namespace pipeline {

// Читает скомпилированный SPIR-V файл и создаёт из него VkShaderModule
VkShaderModule loadShaderModule(const char* filename) {
	const std::string path = std::string(SHADER_DIR) + filename;

	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		std::cerr << "Failed to open shader file: " << path << '\n';
		return VK_NULL_HANDLE;
	}

	// Открыли файл с позицией в конце (ios::ate), поэтому tellg() сразу даёт его размер
	const size_t size = size_t(file.tellg());
	std::vector<char> code(size);
	file.seekg(0);
	file.read(code.data(), std::streamsize(size));
	file.close();

	const VkShaderModuleCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size(),
		.pCode = reinterpret_cast<const uint32_t*>(code.data()),
	};

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(graphics::internal::context.device, &create_info, nullptr,
							 &module) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module: " << path << '\n';
		return VK_NULL_HANDLE;
	}

	return module;
}

// Создаёт pipeline layout: какие наборы дескрипторов и push constant увидят шейдеры
bool createLayout(std::span<const VkDescriptorSetLayout> set_layouts,
				  std::span<const VkPushConstantRange> push_constants, VkPipelineLayout& out) {
	const VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = uint32_t(set_layouts.size()),
		.pSetLayouts = set_layouts.data(),
		.pushConstantRangeCount = uint32_t(push_constants.size()),
		.pPushConstantRanges = push_constants.data(),
	};

	if (vkCreatePipelineLayout(graphics::internal::context.device, &layout_info, nullptr, &out) !=
		VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	return true;
}

// Создаёт graphics pipeline: шейдеры, формат вершин, растеризация, блендинг
bool create(const Config& config, VkPipeline& out) {
	auto& context = graphics::internal::context;

	// Без фрагментного шейдера pipeline только пишет глубину — так рисуется карта теней
	const bool has_fragment = config.fragment_shader != nullptr;

	const VkShaderModule vertex_shader = loadShaderModule(config.vertex_shader);
	const VkShaderModule fragment_shader = has_fragment ? loadShaderModule(config.fragment_shader) : VK_NULL_HANDLE;
	if (vertex_shader == VK_NULL_HANDLE || (has_fragment && fragment_shader == VK_NULL_HANDLE)) {
		vkDestroyShaderModule(context.device, vertex_shader, nullptr);
		vkDestroyShaderModule(context.device, fragment_shader, nullptr);
		return false;
	}

	const VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader,
			.pName = "main",
		},
	};

	// Формат вершины общий для всех пайплайнов — geometry::Vertex
	const VkVertexInputBindingDescription binding = geometry::vertexBinding();
	const auto attributes = geometry::vertexAttributes();

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = uint32_t(attributes.size()),
		.pVertexAttributeDescriptions = attributes.data(),
	};

	// Топология примитивов задаётся конфигом: треугольники для мешей, линии для вспомогательной геометрии
	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = config.topology,
	};

	// Сами значения viewport/scissor не заданы здесь — они динамические (см. dynamic_state ниже)
	const VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	// Передние грани — с обходом против часовой стрелки, так заданы индексы в geometry.
	// Depth bias сдвигает записываемую глубину (используется картой теней против «акне»)
	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = config.cull_mode,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = config.depth_bias ? VK_TRUE : VK_FALSE,
		.lineWidth = 1.0f,
	};

	// Мультисемплинг отключён — один сэмпл на пиксель
	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	// Тест глубины включён всегда; запись глубины можно отключить конфигом
	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = config.depth_write ? VK_TRUE : VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	// Блендинг отключён, пишем во все 4 канала (RGBA)
	const VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	// Число привязок должно совпадать с числом цветовых привязок render pass (у карты теней их нет)
	const VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = config.color_attachment_count,
		.pAttachments = &color_blend_attachment,
	};

	// Viewport и scissor задаются в command buffer каждый кадр, без пересоздания pipeline (важно при ресайзе окна);
	// depth bias — тоже, чтобы его можно было подбирать из UI. Он последний в списке и включается только по конфигу
	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = uint32_t(sizeof(dynamic_states) / sizeof(dynamic_states[0])) - (config.depth_bias ? 0u : 1u),
		.pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = has_fragment ? 2u : 1u,
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = config.layout,
		.renderPass = config.render_pass != VK_NULL_HANDLE ? config.render_pass : context.render_pass,
		.subpass = 0,
	};

	const VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
													   nullptr, &out);

	// Шейдерные модули больше не нужны после создания pipeline — сразу их уничтожаем
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan graphics pipeline\n";
		return false;
	}

	return true;
}

} // namespace pipeline

#include "application.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include "mesh.hpp"
#include "vk_buffer.hpp"

namespace application {

namespace {

struct UBO {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

vk_buffer::Buffer vertex_buffer;
vk_buffer::Buffer index_buffer;
vk_buffer::Buffer uniform_buffer;
vk_buffer::Buffer uniform_buffer_2;

bool use_perspective_projection = true;

glm::vec3 transform_position = glm::vec3(0.0f);
glm::vec3 transform_rotation_degrees = glm::vec3(0.0f);
glm::vec3 transform_scale = glm::vec3(1.0f);

bool animate_enabled = false;
bool animate_paused = false;
float animation_speed = 1.0f;
float trajectory_radius = 2.0f;
double elapsed_anim_time = 0.0;
double last_update_time = -1.0;

constexpr float kOrbitAngularSpeed = 1.0f;        // radians per unit of elapsed animation time
constexpr float kSpinAngularSpeedDegrees = 90.0f; // degrees per unit of elapsed animation time

glm::vec4 base_color = glm::vec4(1.0f);

glm::mat4 computeModelMatrix() {
	glm::vec3 position = transform_position;
	glm::vec3 rotation_degrees = transform_rotation_degrees;

	if (animate_enabled) {
		const double angle = elapsed_anim_time * kOrbitAngularSpeed;
		position = glm::vec3(trajectory_radius * float(std::cos(angle)), 0.0f,
							 trajectory_radius * float(std::sin(angle)));
		rotation_degrees.y = float(elapsed_anim_time * kSpinAngularSpeedDegrees);
	}

	glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
	model = glm::rotate(model, glm::radians(rotation_degrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation_degrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation_degrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::scale(model, transform_scale);
	return model;
}

VkDescriptorSetLayout descriptor_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet descriptor_set;
VkDescriptorSet descriptor_set_2;

// Second cube: fixed offset, independent constant auto-rotation, fixed tint,
const glm::vec3 kSecondCubeOffset = glm::vec3(2.0f, 0.0f, 0.0f);
const glm::vec4 kSecondCubeColor = glm::vec4(1.0f, 0.55f, 0.15f, 1.0f);
constexpr float kSecondCubeSpinDegreesPerSecond = 30.0f;

VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

VkShaderModule loadShaderModule(const char* filename) {
	const std::string path = std::string(SHADER_DIR) + filename;

	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		std::cerr << "Failed to open shader file: " << path << '\n';
		return VK_NULL_HANDLE;
	}

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

bool createBuffers() {
	const VkDeviceSize vertex_size = sizeof(mesh::cube_vertices[0]) * mesh::cube_vertices.size();
	if (!vk_buffer::create(vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer)) {
		return false;
	}
	std::memcpy(vertex_buffer.mapped, mesh::cube_vertices.data(), size_t(vertex_size));

	const VkDeviceSize index_size = sizeof(mesh::cube_indices[0]) * mesh::cube_indices.size();
	if (!vk_buffer::create(index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		return false;
	}
	std::memcpy(index_buffer.mapped, mesh::cube_indices.data(), size_t(index_size));

	if (!vk_buffer::create(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer)) {
		return false;
	}

	if (!vk_buffer::create(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer_2)) {
		return false;
	}

	return true;
}

bool createDescriptors() {
	auto& context = graphics::internal::context;

	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr,
									&descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
		return false;
	}

	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 4,
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 4,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	const VkDescriptorSetLayout set_layouts[2] = { descriptor_set_layout, descriptor_set_layout };
	VkDescriptorSet sets[2] = {};

	const VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 2,
		.pSetLayouts = set_layouts,
	};

	if (vkAllocateDescriptorSets(context.device, &alloc_info, sets) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor sets\n";
		return false;
	}

	descriptor_set = sets[0];
	descriptor_set_2 = sets[1];

	const VkDescriptorBufferInfo buffer_infos[2] = {
		{ .buffer = uniform_buffer.buffer, .offset = 0, .range = sizeof(UBO) },
		{ .buffer = uniform_buffer_2.buffer, .offset = 0, .range = sizeof(UBO) },
	};

	const VkWriteDescriptorSet writes[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_infos[0],
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set_2,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_infos[1],
		},
	};

	vkUpdateDescriptorSets(context.device, 2, writes, 0, nullptr);

	return true;
}

bool createPipeline() {
	auto& context = graphics::internal::context;

	const VkShaderModule vertex_shader = loadShaderModule("cube.vert.spv");
	const VkShaderModule fragment_shader = loadShaderModule("cube.frag.spv");
	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
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

	const VkVertexInputBindingDescription binding = {
		.binding = 0,
		.stride = sizeof(mesh::Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	const VkVertexInputAttributeDescription attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(mesh::Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(mesh::Vertex, color),
		},
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = sizeof(attributes) / sizeof(attributes[0]),
		.pVertexAttributeDescriptions = attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	const VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	const VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	const VkPushConstantRange push_constant_range = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(glm::vec4),
	};

	const VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant_range,
	};

	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	const VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
													   nullptr, &pipeline);

	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan graphics pipeline\n";
		return false;
	}

	return true;
}

} // namespace

bool initialize() {
	if (!createBuffers()) {
		return false;
	}

	if (!createDescriptors()) {
		return false;
	}

	if (!createPipeline()) {
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);

	vk_buffer::destroy(uniform_buffer);
	vk_buffer::destroy(uniform_buffer_2);
	vk_buffer::destroy(vertex_buffer);
	vk_buffer::destroy(index_buffer);
}

void update(double time) {
	const double dt = (last_update_time >= 0.0) ? (time - last_update_time) : 0.0;
	last_update_time = time;

	if (animate_enabled && !animate_paused) {
		elapsed_anim_time += dt * double(animation_speed);
	}

	ImGui::Begin("Cube Controls");

	ImGui::SeparatorText("Projection");
	if (ImGui::RadioButton("Perspective", use_perspective_projection)) {
		use_perspective_projection = true;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", !use_perspective_projection)) {
		use_perspective_projection = false;
	}

	ImGui::SeparatorText("Transform");
	ImGui::BeginDisabled(animate_enabled);
	ImGui::SliderFloat3("Position", &transform_position.x, -3.0f, 3.0f);
	ImGui::SliderFloat3("Rotation", &transform_rotation_degrees.x, -180.0f, 180.0f);
	ImGui::EndDisabled();
	ImGui::SliderFloat3("Scale", &transform_scale.x, 0.1f, 3.0f);

	ImGui::SeparatorText("Animation");
	ImGui::Checkbox("Animate", &animate_enabled);
	ImGui::BeginDisabled(!animate_enabled);
	ImGui::SameLine();
	if (ImGui::Button(animate_paused ? "Play" : "Pause")) {
		animate_paused = !animate_paused;
	}
	ImGui::EndDisabled();
	ImGui::SliderFloat("Speed", &animation_speed, 0.0f, 5.0f);
	ImGui::SliderFloat("Radius", &trajectory_radius, 0.1f, 5.0f);

	ImGui::SeparatorText("Color");
	ImGui::ColorEdit4("Cube Color", &base_color.x);

	ImGui::End();

	ImGui::ShowDemoWindow();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);

	UBO ubo{};
	ubo.model = computeModelMatrix();
	ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

	if (use_perspective_projection) {
		ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
	} else {
		constexpr float half_extent = 1.7f;
		ubo.proj = glm::ortho(-half_extent * aspect, half_extent * aspect, -half_extent, half_extent,
							  0.1f, 100.0f);
	}
	ubo.proj[1][1] *= -1.0f;

	std::memcpy(uniform_buffer.mapped, &ubo, sizeof(ubo));

	UBO ubo2{};
	ubo2.model = glm::rotate(glm::translate(glm::mat4(1.0f), kSecondCubeOffset),
							 glm::radians(float(last_update_time) * kSecondCubeSpinDegreesPerSecond),
							 glm::vec3(0.0f, 1.0f, 0.0f));
	ubo2.view = ubo.view;
	ubo2.proj = ubo.proj;

	std::memcpy(uniform_buffer_2.mapped, &ubo2, sizeof(ubo2));

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	vkBeginCommandBuffer(fd.command_buffer, &begin_info);

	const VkClearValue clear_values[] = {
		{ .color = { { 0.02f, 0.02f, 0.03f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = { .extent = context.swapchain_extent };
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&descriptor_set, 0, nullptr);

	vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					   sizeof(glm::vec4), &base_color);

	const VkBuffer vertex_buffers[] = { vertex_buffer.buffer };
	const VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, vertex_buffers, offsets);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdDrawIndexed(fd.command_buffer, uint32_t(mesh::cube_indices.size()), 1, 0, 0, 0);

	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&descriptor_set_2, 0, nullptr);

	vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					   sizeof(glm::vec4), &kSecondCubeColor);

	vkCmdDrawIndexed(fd.command_buffer, uint32_t(mesh::cube_indices.size()), 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application

#include "model.hpp"

#include <cstring>
#include <iostream>

#include <imgui.h>

#include "graphics_internal.hpp"

namespace {

// Данные, передаваемые в вершинный шейдер через uniform-буфер модели
struct UBO {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

} // namespace

Model::Model(const Description& description)
	: name(description.name), mesh(description.mesh), transform(description.transform),
	  animation(description.animation), color(description.color) {}

bool Model::create(VkDescriptorSetLayout layout, VkDescriptorPool pool) {
	auto& context = graphics::internal::context;

	if (!vk_buffer::create(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer)) {
		return false;
	}

	const VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};

	if (vkAllocateDescriptorSets(context.device, &alloc_info, &descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor set for model " << name << '\n';
		return false;
	}

	// Привязываем набор дескрипторов к uniform-буферу этой модели
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = uniform_buffer.buffer,
		.offset = 0,
		.range = sizeof(UBO),
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info,
	};

	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);

	return true;
}

void Model::destroy() {
	vk_buffer::destroy(uniform_buffer);
}

void Model::update(double dt) {
	animation.update(dt);
}

// Копируем MVP напрямую в замапленную память uniform-буфера модели
void Model::uploadUniforms(const glm::mat4& view, const glm::mat4& proj) {
	const UBO ubo = {
		.model = animation.apply(transform).matrix(),
		.view = view,
		.proj = proj,
	};
	std::memcpy(uniform_buffer.mapped, &ubo, sizeof(ubo));
}

// Меш привязывается заново для каждой модели — модели могут рисовать разные меши
void Model::draw(VkCommandBuffer command_buffer, VkPipelineLayout pipeline_layout) const {
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&descriptor_set, 0, nullptr);

	vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					   sizeof(glm::vec4), &color);

	mesh->bind(command_buffer);
	mesh->draw(command_buffer);
}

// PushID разделяет одинаковые подписи виджетов ("Position", "Animate", ...) разных моделей;
// ID берётся из имени, а не из адреса, чтобы состояние UI переживало переаллокацию вектора моделей
void Model::drawUi() {
	ImGui::PushID(name.c_str());
	if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		transform.drawUi();
		animation.drawUi();

		ImGui::SeparatorText("Color");
		ImGui::ColorEdit4("Color", &color.x);
	}
	ImGui::PopID();
}

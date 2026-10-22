#include "model.hpp"

#include <cstring>
#include <iostream>

#include <imgui.h>

#include "graphics_internal.hpp"
#include "shader_types.hpp"

using shader::ModelUniforms;

Model::Model(const Description& description)
	: name(description.name), mesh(description.mesh), transform(description.transform),
	  animation(description.animation), color(description.color) {}

bool Model::create(VkDescriptorSetLayout model_set_layout, VkDescriptorPool pool) {
	auto& context = graphics::internal::context;

	if (!vk_buffer::create(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer)) {
		return false;
	}

	const VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &model_set_layout,
	};

	if (vkAllocateDescriptorSets(context.device, &alloc_info, &descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor set for model " << name << '\n';
		return false;
	}

	// Привязываем набор дескрипторов к uniform-буферу этой модели
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = uniform_buffer.buffer,
		.offset = 0,
		.range = sizeof(ModelUniforms),
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

// Копируем матрицу модели и цвет напрямую в замапленную память uniform-буфера модели
void Model::uploadUniforms() {
	const ModelUniforms uniforms = {
		.model = animation.apply(transform).matrix(),
		.color = color,
	};
	std::memcpy(uniform_buffer.mapped, &uniforms, sizeof(uniforms));
}

// Набор модели привязывается в слот set = 1; set = 0 (данные сцены) уже привязан сценой.
// Меш привязывается заново для каждой модели — модели могут рисовать разные меши
void Model::draw(VkCommandBuffer command_buffer, VkPipelineLayout pipeline_layout) const {
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1,
							&descriptor_set, 0, nullptr);

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

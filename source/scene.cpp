#include "scene.hpp"

#include <cstring>
#include <iostream>

#include <glm/glm.hpp>

#include <imgui.h>

#include "graphics_internal.hpp"
#include "pipeline.hpp"
#include "shader_types.hpp"

using shader::SceneUniforms;

namespace {

// Создаёт layout набора из одного uniform-буфера в binding 0, видимого вершинному и фрагментному шейдерам
bool createUniformSetLayout(VkDescriptorSetLayout& out) {
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(graphics::internal::context.device, &layout_info, nullptr, &out) !=
		VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
		return false;
	}

	return true;
}

} // namespace

bool Scene::create() {
	if (!cube_mesh.create(geometry::makeCube())) {
		return false;
	}

	if (!createDescriptors()) {
		return false;
	}

	if (!createModels()) {
		return false;
	}

	if (!createPipeline()) {
		return false;
	}

	return true;
}

void Scene::destroy() {
	auto& context = graphics::internal::context;

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	for (Model& model : models) {
		model.destroy();
	}

	vk_buffer::destroy(scene_uniform_buffer);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);

	cube_mesh.destroy();
}

void Scene::update(double dt) {
	camera.update(dt);

	for (Model& model : models) {
		model.update(dt);
	}
}

void Scene::drawUi() {
	ImGui::Begin("Scene");

	camera.drawUi();
	lighting.drawUi();

	for (Model& model : models) {
		model.drawUi();
	}

	ImGui::End();
}

// Запись в uniform-буферы безопасна: prepare() уже дождался завершения предыдущего кадра
void Scene::record(VkCommandBuffer command_buffer, float aspect) {
	SceneUniforms scene_uniforms = {
		.view_projection = camera.projection(aspect) * camera.view(),
		.camera_position = glm::vec4(camera.eyePosition(), 1.0f),
	};
	lighting.fill(scene_uniforms);
	std::memcpy(scene_uniform_buffer.mapped, &scene_uniforms, sizeof(scene_uniforms));

	for (Model& model : models) {
		model.uploadUniforms();
	}

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Набор сцены (set 0) привязывается один раз за кадр; модели меняют только set 1
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&scene_descriptor_set, 0, nullptr);

	for (const Model& model : models) {
		model.draw(command_buffer, pipeline_layout);
	}
}

// Создаёт layout'ы наборов сцены и моделей, пул дескрипторов и набор сцены с его uniform-буфером;
// наборы моделей выделяют сами модели в Model::create
bool Scene::createDescriptors() {
	auto& context = graphics::internal::context;

	if (!createUniformSetLayout(scene_set_layout) || !createUniformSetLayout(model_set_layout)) {
		return false;
	}

	// Один набор сцены плюс по одному набору на модель, в каждом по одному uniform-буферу.
	// Storage-буферы зарезервированы заранее под массивы источников света (точечные и прожекторы)
	const VkDescriptorPoolSize pool_sizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 + kMaxModels },
		{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1 + kMaxModels,
		.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
		.pPoolSizes = pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	if (!vk_buffer::create(sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
						   scene_uniform_buffer)) {
		return false;
	}

	const VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &scene_set_layout,
	};

	if (vkAllocateDescriptorSets(context.device, &alloc_info, &scene_descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor set for scene\n";
		return false;
	}

	// Привязываем набор сцены к её uniform-буферу
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = scene_uniform_buffer.buffer,
		.offset = 0,
		.range = sizeof(SceneUniforms),
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = scene_descriptor_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info,
	};

	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);

	return true;
}

// Создаёт модели сцены и их GPU-ресурсы; меш и дескрипторы уже должны быть созданы
bool Scene::createModels() {
	// Первый куб: трансформация и анимация по орбите управляются из UI
	models.emplace_back(Model::Description{
		.name = "Cube",
		.mesh = &cube_mesh,
	});

	// Второй куб: стоит в точке (2, 0, 0) и постоянно вращается вокруг Y со скоростью 30°/с без орбиты
	models.emplace_back(Model::Description{
		.name = "Second Cube",
		.mesh = &cube_mesh,
		.transform = { .position = glm::vec3(2.0f, 0.0f, 0.0f) },
		.animation = Animation(true, 0.0f, 0.0f, glm::vec3(0.0f, 30.0f, 0.0f)),
		.material = { .albedo = glm::vec3(1.0f, 0.55f, 0.15f) },
	});

	if (models.size() > kMaxModels) {
		std::cerr << "Too many models in scene: " << models.size() << " > " << kMaxModels << '\n';
		return false;
	}

	for (Model& model : models) {
		if (!model.create(model_set_layout, descriptor_pool)) {
			return false;
		}
	}

	return true;
}

// Создаёт pipeline layout (set 0 — сцена, set 1 — модель) и сам graphics pipeline
bool Scene::createPipeline() {
	const VkDescriptorSetLayout set_layouts[] = { scene_set_layout, model_set_layout };

	if (!pipeline::createLayout(set_layouts, {}, pipeline_layout)) {
		return false;
	}

	return pipeline::create({
		.vertex_shader = "cube.vert.spv",
		.fragment_shader = "cube.frag.spv",
		.layout = pipeline_layout,
	}, pipeline);
}

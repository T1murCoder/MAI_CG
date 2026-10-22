#include "scene.hpp"

#include <iostream>

#include <glm/glm.hpp>

#include <imgui.h>

#include "graphics_internal.hpp"
#include "pipeline.hpp"

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

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);

	cube_mesh.destroy();
}

void Scene::update(double dt) {
	for (Model& model : models) {
		model.update(dt);
	}
}

void Scene::drawUi() {
	ImGui::Begin("Scene");

	camera.drawUi();

	for (Model& model : models) {
		model.drawUi();
	}

	ImGui::End();
}

// Запись в uniform-буферы безопасна: prepare() уже дождался завершения предыдущего кадра
void Scene::record(VkCommandBuffer command_buffer, float aspect) {
	const glm::mat4 view = camera.view();
	const glm::mat4 proj = camera.projection(aspect);

	for (Model& model : models) {
		model.uploadUniforms(view, proj);
	}

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	for (const Model& model : models) {
		model.draw(command_buffer, pipeline_layout);
	}
}

// Создаёт layout и пул дескрипторов; сами наборы выделяют модели в Model::create
bool Scene::createDescriptors() {
	auto& context = graphics::internal::context;

	// Привязка 0: один uniform-буфер, используется в вершинном шейдере
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

	// По одному набору с одним uniform-буфером на каждую модель
	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = kMaxModels,
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = kMaxModels,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

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
		.color = glm::vec4(1.0f, 0.55f, 0.15f, 1.0f),
	});

	models.emplace_back(Model::Description{
		.name = "Third Cube",
		.mesh = &cube_mesh,
		.transform = { .position = glm::vec3(-2.0f, 0.0f, 0.0f) },
	});


	if (models.size() > kMaxModels) {
		std::cerr << "Too many models in scene: " << models.size() << " > " << kMaxModels << '\n';
		return false;
	}

	for (Model& model : models) {
		if (!model.create(descriptor_set_layout, descriptor_pool)) {
			return false;
		}
	}

	return true;
}

// Создаёт pipeline layout (набор дескрипторов + push constant с цветом) и сам graphics pipeline
bool Scene::createPipeline() {
	// Push constant с цветом модели (vec4), доступен фрагментному шейдеру
	const VkPushConstantRange push_constant_range = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(glm::vec4),
	};

	if (!pipeline::createLayout({ &descriptor_set_layout, 1 }, { &push_constant_range, 1 },
								pipeline_layout)) {
		return false;
	}

	return pipeline::create({
		.vertex_shader = "cube.vert.spv",
		.fragment_shader = "cube.frag.spv",
		.layout = pipeline_layout,
	}, pipeline);
}

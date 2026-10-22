#include "scene.hpp"

#include <cstring>
#include <iostream>
#include <span>

#include <glm/glm.hpp>

#include <imgui.h>

#include "graphics_internal.hpp"
#include "pipeline.hpp"
#include "shader_types.hpp"

using shader::SceneUniforms;

namespace {

// Uniform-буфер в binding 0 набора — данные сцены или модели, нужны обоим шейдерам
constexpr VkDescriptorSetLayoutBinding kUniformBinding = {
	.binding = 0,
	.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	.descriptorCount = 1,
	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
};

// Storage-буфер с массивом источников света — читается только во фрагментном шейдере
constexpr VkDescriptorSetLayoutBinding storageBinding(uint32_t binding) {
	return {
		.binding = binding,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};
}

// Создаёт layout набора дескрипторов из списка привязок
bool createSetLayout(std::span<const VkDescriptorSetLayoutBinding> bindings, VkDescriptorSetLayout& out) {
	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = uint32_t(bindings.size()),
		.pBindings = bindings.data(),
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
	if (!cube_mesh.create(geometry::makeCube()) || !white_cube_mesh.create(geometry::makeCube(false)) ||
		!plane_mesh.create(geometry::makePlane())) {
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

	light_gizmos.destroy();
	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	for (Model& model : models) {
		model.destroy();
	}

	lighting.destroy();
	vk_buffer::destroy(scene_uniform_buffer);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);

	plane_mesh.destroy();
	white_cube_mesh.destroy();
	cube_mesh.destroy();
}

void Scene::update(double dt) {
	camera.update(dt);
	lighting.update(dt);

	for (Model& model : models) {
		model.update(dt);
	}
}

void Scene::drawUi() {
	ImGui::Begin("Scene");

	camera.drawUi();
	lighting.drawUi();
	light_gizmos.drawUi();

	for (Model& model : models) {
		model.drawUi();
	}

	ImGui::End();
}

// Запись в буферы и пересоздание storage-буферов безопасны: prepare() уже дождался завершения
// предыдущего кадра, а набор сцены в этом кадре ещё не привязан
void Scene::record(VkCommandBuffer command_buffer, float aspect) {
	SceneUniforms scene_uniforms = {};
	camera.fill(scene_uniforms, aspect);
	lighting.upload(scene_uniforms);
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

	// Тот же pipeline layout — набор сцены (set 0) остаётся привязанным
	light_gizmos.record(command_buffer, lighting);
}

// Создаёт layout'ы наборов сцены и моделей, пул дескрипторов и набор сцены с его uniform-буфером
// и storage-буферами источников света; наборы моделей выделяют сами модели в Model::create
bool Scene::createDescriptors() {
	auto& context = graphics::internal::context;

	// Набор сцены: uniform-буфер (binding 0), массивы точечных источников (binding 1) и прожекторов (binding 2);
	// набор модели: uniform-буфер
	const VkDescriptorSetLayoutBinding scene_bindings[] = { kUniformBinding, storageBinding(1), storageBinding(2) };
	if (!createSetLayout(scene_bindings, scene_set_layout) ||
		!createSetLayout({ &kUniformBinding, 1 }, model_set_layout)) {
		return false;
	}

	// Один набор сцены плюс по одному набору на модель, в каждом по одному uniform-буферу.
	// Storage-буферы — массивы источников света в наборе сцены (точечные и прожекторы)
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

	// Storage-буферы источников света сами привязываются к своим binding набора сцены
	return lighting.create(scene_descriptor_set);
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

	models.emplace_back(Model::Description{
		.name = "Floor",
		.mesh = &plane_mesh,
		.transform = { .position = glm::vec3(0.0f, -1.0f, 0.0f), .scale = glm::vec3(12.0f, 1.0f, 12.0f) },
		.material = { .albedo = glm::vec3(0.8f), .specular = glm::vec3(0.2f), .shininess = 16.0f },
	});

	const Material wall_material = { .albedo = glm::vec3(0.7f), .specular = glm::vec3(0.1f), .shininess = 8.0f };
	models.emplace_back(Model::Description{
		.name = "Back Wall",
		.mesh = &plane_mesh,
		.transform = { .position = glm::vec3(0.0f, 2.0f, -6.0f), .rotation_degrees = glm::vec3(90.0f, 0.0f, 0.0f),
					   .scale = glm::vec3(12.0f, 1.0f, 6.0f) },
		.material = wall_material,
	});
	models.emplace_back(Model::Description{
		.name = "Left Wall",
		.mesh = &plane_mesh,
		.transform = { .position = glm::vec3(-6.0f, 2.0f, 0.0f), .rotation_degrees = glm::vec3(0.0f, 0.0f, -90.0f),
					   .scale = glm::vec3(6.0f, 1.0f, 12.0f) },
		.material = wall_material,
	});

	// Кубы с контрастными материалами: металлический (тёмный, с ярким резким бликом) и матовый (без блика)
	models.emplace_back(Model::Description{
		.name = "Metal Cube",
		.mesh = &white_cube_mesh,
		.transform = { .position = glm::vec3(-3.0f, -0.5f, -3.0f) },
		.material = { .albedo = glm::vec3(0.25f), .specular = glm::vec3(1.0f), .shininess = 256.0f },
	});
	models.emplace_back(Model::Description{
		.name = "Matte Cube",
		.mesh = &white_cube_mesh,
		.transform = { .position = glm::vec3(0.0f, -0.5f, -4.0f) },
		.material = { .albedo = glm::vec3(0.3f, 0.75f, 0.4f), .specular = glm::vec3(0.0f), .shininess = 1.0f },
	});

	// Белый куб на полу со своей стеной-фоном: на него с боков светят тёплый и холодный прожекторы
	// (см. Lighting::spot_lights). Блик слабый, чтобы не «белить» окрашенные светом грани
	models.emplace_back(Model::Description{
		.name = "Spotlit Cube",
		.mesh = &white_cube_mesh,
		.transform = { .position = glm::vec3(4.0f, -0.5f, -2.0f) },
		.material = { .albedo = glm::vec3(0.9f), .specular = glm::vec3(0.2f), .shininess = 64.0f },
	});
	models.emplace_back(Model::Description{
		.name = "Spotlit Cube Wall",
		.mesh = &plane_mesh,
		.transform = { .position = glm::vec3(4.0f, 1.0f, -3.5f), .rotation_degrees = glm::vec3(90.0f, 0.0f, 0.0f),
					   .scale = glm::vec3(4.0f, 1.0f, 4.0f) },
		.material = wall_material,
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

// Создаёт общий pipeline layout (set 0 — сцена, set 1 — модель, push constant визуализаций),
// pipeline моделей и пайплайны визуализации источников. Шейдеры моделей push constant не используют,
// а неосвещаемые шейдеры — набор модели; layout это допускает
bool Scene::createPipeline() {
	const VkDescriptorSetLayout set_layouts[] = { scene_set_layout, model_set_layout };

	if (!pipeline::createLayout(set_layouts, { &LightGizmos::kPushConstantRange, 1 }, pipeline_layout)) {
		return false;
	}

	if (!pipeline::create({
			.vertex_shader = "lit.vert.spv",
			.fragment_shader = "lit.frag.spv",
			.layout = pipeline_layout,
		}, pipeline)) {
		return false;
	}

	// Маркер источника — маленький куб из общего меша куба
	return light_gizmos.create(pipeline_layout, &cube_mesh);
}

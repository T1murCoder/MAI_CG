#include "scene.hpp"

#include <cstring>
#include <iostream>
#include <limits>
#include <span>

#include <glm/glm.hpp>

#include <imgui.h>

#include "graphics_internal.hpp"
#include "pipeline.hpp"
#include "shader_types.hpp"

using shader::SceneUniforms;
using shader::ModelUniforms;

namespace {

// Uniform-буфер сцены в binding 0; нужен обоим шейдерам
constexpr VkDescriptorSetLayoutBinding kUniformBinding = {
	.binding = 0,
	.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	.descriptorCount = 1,
	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
};

constexpr VkDescriptorSetLayoutBinding kModelUniformBinding = {
	.binding = 0,
	.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
	.descriptorCount = 1,
	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
};

constexpr VkDescriptorSetLayoutBinding kAlbedoBinding = {
	.binding = 1,
	.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	.descriptorCount = 1,
	.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
};

bool isSrgbFormat(VkFormat format) {
	switch (format) {
	case VK_FORMAT_R8_SRGB:
	case VK_FORMAT_R8G8_SRGB:
	case VK_FORMAT_R8G8B8_SRGB:
	case VK_FORMAT_B8G8R8_SRGB:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
		return true;
	default:
		return false;
	}
}

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
		!plane_mesh.create(geometry::makePlane()) || !albedo_texture.create(TEXTURE_DIR "uv_grid.png") ||
		!createModels() || !createDescriptors() || !createPipeline()) {
		// В том числе освобождаем уже созданные ресурсы при частичной ошибке инициализации.
		destroy();
		return false;
	}

	return true;
}

void Scene::destroy() {
	auto& context = graphics::internal::context;

	light_gizmos.destroy();
	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	models.clear();

	lighting.destroy();
	vk_buffer::destroy(model_uniform_buffer);
	vk_buffer::destroy(scene_uniform_buffer);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);
	albedo_texture.destroy();

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

// Единственное место, где можно писать в GPU-буферы (memcpy, пересоздание storage-буферов,
// vkUpdateDescriptorSets): prepare() уже дождался завершения предыдущего кадра, а набор сцены
// в этом кадре ещё не привязан. В update() этого делать нельзя — он выполняется до ожидания fence
void Scene::record(VkCommandBuffer command_buffer, float aspect) {
	SceneUniforms scene_uniforms = {};
	camera.fill(scene_uniforms, aspect);
	lighting.upload(scene_uniforms);
	// Проверяем реальный формат каждый кадр, в том числе после пересоздания swapchain.
	scene_uniforms.tone_mapping.z = isSrgbFormat(graphics::internal::context.swapchain_format) ? 0.0f : 1.0f;
	std::memcpy(scene_uniform_buffer.mapped, &scene_uniforms, sizeof(scene_uniforms));
	if (!vk_buffer::flush(scene_uniform_buffer)) {
		return;
	}

	for (size_t i = 0; i < models.size(); ++i) {
		const ModelUniforms uniforms = models[i].uniforms();
		auto* destination = static_cast<std::byte*>(model_uniform_buffer.mapped) + i * model_uniform_stride;
		std::memcpy(destination, &uniforms, sizeof(uniforms));
	}
	if (!vk_buffer::flush(model_uniform_buffer)) {
		return;
	}

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Набор сцены (set 0) привязывается один раз за кадр
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&scene_descriptor_set, 0, nullptr);

	for (size_t i = 0; i < models.size(); ++i) {
		// Диапазон и представимость offset в uint32_t проверены при создании буфера.
		const uint32_t dynamic_offset = uint32_t(i * model_uniform_stride);
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1,
								&model_descriptor_set, 1, &dynamic_offset);
		models[i].draw(command_buffer);
	}

	// Тот же pipeline layout — набор сцены (set 0) остаётся привязанным
	light_gizmos.record(command_buffer, lighting);
}

// Создаёт два общих набора: сцена (UBO + SSBO света) и модели (dynamic UBO + albedo)
bool Scene::createDescriptors() {
	auto& context = graphics::internal::context;

	// Набор сцены: uniform-буфер (binding 0), массивы точечных источников (binding 1) и прожекторов (binding 2);
	// набор моделей: dynamic uniform-буфер и общая albedo-текстура
	const VkDescriptorSetLayoutBinding scene_bindings[] = { kUniformBinding, storageBinding(1), storageBinding(2) };
	const VkDescriptorSetLayoutBinding model_bindings[] = { kModelUniformBinding, kAlbedoBinding };
	if (!createSetLayout(scene_bindings, scene_set_layout) ||
		!createSetLayout(model_bindings, model_set_layout)) {
		return false;
	}

	// Один набор сцены и один общий набор моделей.
	// Storage-буферы — массивы источников света в наборе сцены (точечные и прожекторы)
	const VkDescriptorPoolSize pool_sizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = 1 },
		{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
		{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 2,
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

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(context.physical_device, &properties);
	const auto& limits = properties.limits;
	const VkDeviceSize alignment = limits.minUniformBufferOffsetAlignment;
	if (models.empty() || alignment == 0 || sizeof(ModelUniforms) > limits.maxUniformBufferRange ||
		limits.maxDescriptorSetUniformBuffersDynamic < 1) {
		std::cerr << "Device limits do not support the model dynamic UBO\n";
		return false;
	}
	model_uniform_stride = ((sizeof(ModelUniforms) + alignment - 1) / alignment) * alignment;
	if (model_uniform_stride > std::numeric_limits<size_t>::max() / models.size() ||
		(models.size() - 1) > std::numeric_limits<uint32_t>::max() / model_uniform_stride) {
		std::cerr << "Model dynamic UBO size or offsets exceed addressable limits\n";
		return false;
	}
	// При i < models.size(): i * stride + sizeof(ModelUniforms) <= buffer_size,
	// а offset кратен minUniformBufferOffsetAlignment и представим в uint32_t.
	const VkDeviceSize buffer_size = models.size() * model_uniform_stride;
	if (!vk_buffer::create(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, model_uniform_buffer)) {
		return false;
	}
	const VkDescriptorSetAllocateInfo model_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &model_set_layout,
	};
	if (vkAllocateDescriptorSets(context.device, &model_alloc_info, &model_descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate shared model descriptor set\n";
		return false;
	}
	const VkDescriptorBufferInfo model_buffer_info = {
		.buffer = model_uniform_buffer.buffer,
		.offset = 0,
		.range = sizeof(ModelUniforms),
	};
	const VkDescriptorImageInfo albedo_info = albedo_texture.descriptor();
	const VkWriteDescriptorSet model_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = model_descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &model_buffer_info,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = model_descriptor_set,
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &albedo_info,
		},
	};
	vkUpdateDescriptorSets(context.device, uint32_t(std::size(model_writes)), model_writes, 0, nullptr);

	// Storage-буферы источников света сами привязываются к своим binding набора сцены
	return lighting.create(scene_descriptor_set);
}

// Создаёт CPU-состояние моделей; меши уже должны быть созданы
bool Scene::createModels() {
	// Первый куб: трансформация и анимация по орбите управляются из UI
	models.emplace_back(Model::Description{
		.name = "Cube",
		.mesh = &cube_mesh,
	});

	// Второй куб: только вращение вокруг Y, без орбиты
	models.emplace_back(Model::Description{
		.name = "Second Cube",
		.mesh = &cube_mesh,
		.transform = { .position = glm::vec3(2.0f, 0.0f, 0.0f) },
		.animation = Animation(true, 0.0f, 0.0f, glm::vec3(0.0f, 30.0f, 0.0f)),
	});

	models.emplace_back(Model::Description{
		.name = "Floor",
		.mesh = &plane_mesh,
		.transform = { .position = glm::vec3(0.0f, -1.0f, 0.0f), .scale = glm::vec3(12.0f, 1.0f, 12.0f) },
		.material = { .specular = glm::vec3(0.2f), .shininess = 16.0f },
	});

	const Material wall_material = { .specular = glm::vec3(0.1f), .shininess = 8.0f };
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

	// Кубы с контрастными бликами: металлический (яркий резкий блик) и матовый (без блика)
	models.emplace_back(Model::Description{
		.name = "Metal Cube",
		.mesh = &white_cube_mesh,
		.transform = { .position = glm::vec3(-3.0f, -0.5f, -3.0f) },
		.material = { .specular = glm::vec3(1.0f), .shininess = 256.0f },
	});
	models.emplace_back(Model::Description{
		.name = "Matte Cube",
		.mesh = &white_cube_mesh,
		.transform = { .position = glm::vec3(0.0f, -0.5f, -4.0f) },
		.material = { .specular = glm::vec3(0.0f), .shininess = 1.0f },
	});

	// Белый куб на полу со своей стеной-фоном: на него с боков светят тёплый и холодный прожекторы
	// (см. Lighting::spot_lights). Блик слабый, чтобы не «белить» окрашенные светом грани
	models.emplace_back(Model::Description{
		.name = "Spotlit Cube",
		.mesh = &white_cube_mesh,
		.transform = { .position = glm::vec3(4.0f, -0.5f, -2.0f) },
		.material = { .specular = glm::vec3(0.2f), .shininess = 64.0f },
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

#include "scene.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>
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

constexpr VkDescriptorSetLayoutBinding kSunShadowBinding = {
	.binding = 3,
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
		!plane_mesh.create(geometry::makePlane()) ||
		!createModels() || !sun_shadow_map.create() || !spot_shadow_map.create() ||
		!createDescriptors() || !createPipeline()) {
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
	shadow_draws.clear();
	materials.destroy();

	lighting.destroy();
	vk_buffer::destroy(model_uniform_buffer);
	vk_buffer::destroy(scene_uniform_buffer);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);
	sun_shadow_map.destroy();
	spot_shadow_map.destroy();
	spot_projection.reset();

	plane_mesh.destroy();
	white_cube_mesh.destroy();
	cube_mesh.destroy();
}

void Scene::update(double dt) {
	camera.update(dt);
	lighting.update(dt);
	// Все движения UV и цветовые колебания в lit.frag периодичны за 120 секунд.
	// Ограничиваем часы, чтобы float в push constants не терял точность при долгой работе.
	albedo_effect_time = std::fmod(albedo_effect_time + dt, 120.0);

	for (Model& model : models) {
		model.update(dt);
	}
}

void Scene::drawUi() {
	ImGui::Begin("Scene");

	drawShadowUi();
	camera.drawUi();
	lighting.drawUi();
	if (lighting.shadowSpotIndex() < 0 && view == View::SpotDepth) {
		view = View::Scene;
	}
	light_gizmos.drawUi();

	for (Model& model : models) {
		model.drawUi(materials);
	}

	ImGui::End();
}

void Scene::drawShadowUi() {
	if (!ImGui::CollapsingHeader("Shadow Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	const float fps = ImGui::GetIO().Framerate;
	ImGui::Text("Frame (with VSync): %.2f ms | %.1f FPS", fps > 0.0f ? 1000.0f / fps : 0.0f, fps);
	ImGui::Checkbox("Sun shadows", &sun_shadows_enabled);
	ImGui::Checkbox("Isolate sun", &isolate_sun);
	if (isolate_sun) {
		ImGui::TextWrapped("Ambient, point and spot lighting are hidden. Their settings are preserved; emissive materials remain visible.");
	}
	lighting.drawShadowSpotUi();
	if (lighting.shadowSpotIndex() < 0 && view == View::SpotDepth) {
		view = View::Scene;
	}
	const char* views[] = { "Scene", "Sun depth", "Spotlight depth" };
	if (ImGui::BeginCombo("View", views[int(view)])) {
		for (int i = 0; i < int(std::size(views)); ++i) {
			const bool selected = view == View(i);
			ImGui::BeginDisabled(View(i) == View::SpotDepth && lighting.shadowSpotIndex() < 0);
			if (ImGui::Selectable(views[i], selected)) {
				view = View(i);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
			ImGui::EndDisabled();
		}
		ImGui::EndCombo();
	}
	ImGui::Text("Sun depth: %u x %u", ShadowMap::kResolution, ShadowMap::kResolution);
	if (view == View::SunDepth) {
		ImGui::TextWrapped("Black: near; white: far / empty. Change the sun area, light direction or a model's transform to inspect the map.");
	} else if (view == View::SpotDepth) {
		ImGui::TextWrapped("Linear depth: black = near, white = far / empty. Edit the selected Spot Light's position, direction, outer angle or animation.");
	}
	if (lighting.shadowSpotIndex() >= 0 && spot_projection) {
		ImGui::Text("Spot depth: %u x %u | %.2f .. %.2f", ShadowMap::kResolution, ShadowMap::kResolution,
			spot_projection->near_plane, spot_projection->far_plane);
	}

	ImGui::SeparatorText("Sun coverage");
	ImGui::DragFloat3("Sun area center", &sun_area.center.x, 0.1f,
		-sun_shadow::Area::kMaxCenter, sun_shadow::Area::kMaxCenter, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Sun area size", &sun_area.size, sun_shadow::Area::kMinSize,
		sun_shadow::Area::kMaxSize, "%.2f", ImGuiSliderFlags_AlwaysClamp);

	ImGui::SeparatorText("Sun depth bias");
	ImGui::SliderFloat("Constant factor", &sun_bias.constant, 0.0f,
		ShadowMap::DepthBias::kMaxConstant, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Slope factor", &sun_bias.slope, 0.0f,
		ShadowMap::DepthBias::kMaxSlope, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::TextWrapped("Increase bias to reduce surface speckles. Too much bias detaches shadows from objects.");
	if (ImGui::Button("Reset sun area and bias")) {
		sun_area = {};
		sun_bias = {};
	}
	sun_area.sanitize();
	sun_bias.sanitize();
	ImGui::Text("World units / texel: %.4f", sun_area.size / ShadowMap::kResolution);
	ImGui::Text("Auto depth range: %.2f .. %.2f", sun_projection.near_plane, sun_projection.far_plane);
}

// Единственное место, где можно писать в GPU-буферы (memcpy, пересоздание storage-буферов,
// vkUpdateDescriptorSets): prepare() уже дождался завершения предыдущего кадра, а набор сцены
// в этом кадре ещё не привязан. В update() этого делать нельзя — он выполняется до ожидания fence.
bool Scene::prepareFrame(float aspect) {
	SceneUniforms scene_uniforms = {};
	camera.fill(scene_uniforms, aspect);
	const auto selected_spot = lighting.upload(scene_uniforms);

	std::array<glm::vec3, kMaxModels * 8> world_bounds;
	size_t bound_count = 0;
	shadow_draws.clear();
	for (size_t i = 0; i < models.size(); ++i) {
		const ModelUniforms uniforms = models[i].uniforms();
		shadow_draws.push_back({ .mesh = &models[i].geometry(), .model = uniforms.model });
		for (const glm::vec3& corner : models[i].geometry().bounds()) {
			world_bounds[bound_count++] = glm::vec3(uniforms.model * glm::vec4(corner, 1.0f));
		}
		auto* destination = static_cast<std::byte*>(model_uniform_buffer.mapped) + i * model_uniform_stride;
		std::memcpy(destination, &uniforms, sizeof(uniforms));
	}
	// Направление и границы взяты из того же снимка кадра, который используют оба прохода.
	sun_projection = sun_shadow::fit(sun_area, glm::vec3(scene_uniforms.light_direction),
		{ world_bounds.data(), bound_count });
	spot_projection = selected_spot ? spot_shadow::fit(*selected_spot, { world_bounds.data(), bound_count }) : std::nullopt;
	if (!spot_projection && view == View::SpotDepth) {
		view = View::Scene;
	}
	scene_uniforms.sun_view_projection = sun_projection.view_projection;
	scene_uniforms.shadow_settings = glm::uvec4(sun_shadows_enabled ? 1u : 0u, isolate_sun ? 1u : 0u, 0u, 0u);
	// Проверяем реальный формат каждый кадр, в том числе после пересоздания swapchain.
	scene_uniforms.tone_mapping.z = isSrgbFormat(graphics::internal::context.swapchain_format) ? 0.0f : 1.0f;
	std::memcpy(scene_uniform_buffer.mapped, &scene_uniforms, sizeof(scene_uniforms));
	return vk_buffer::flush(scene_uniform_buffer) && vk_buffer::flush(model_uniform_buffer);
}

void Scene::recordShadowMaps(VkCommandBuffer command_buffer) {
	sun_shadow_map.record(command_buffer, sun_projection.view_projection, shadow_draws, sun_bias);
	if (spot_projection) {
		spot_shadow_map.record(command_buffer, spot_projection->view_projection, shadow_draws, spot_bias);
	}
}

void Scene::record(VkCommandBuffer command_buffer) {
	if (view == View::SunDepth) {
		sun_shadow_map.recordDebug(command_buffer, graphics::internal::context.swapchain_extent, {});
		return;
	}
	if (view == View::SpotDepth && spot_projection) {
		spot_shadow_map.recordDebug(command_buffer, graphics::internal::context.swapchain_extent, {
			.near_plane = float(spot_projection->near_plane),
			.far_plane = float(spot_projection->far_plane),
			.perspective = true,
		});
		return;
	}

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Набор сцены (set 0) привязывается один раз за кадр
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&scene_descriptor_set, 0, nullptr);

	for (size_t i = 0; i < models.size(); ++i) {
		// Диапазон и представимость offset в uint32_t проверены при создании буфера.
		const uint32_t dynamic_offset = uint32_t(i * model_uniform_stride);
		const VkDescriptorSet material_set = materials.descriptor(models[i].materialId());
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1,
								&material_set, 1, &dynamic_offset);
		const shader::AlbedoEffectPushConstants effect = {
			.animation = glm::vec4(float(albedo_effect_time), models[i].albedoEffectEnabled() ? 1.0f : 0.0f, 0.0f, 0.0f),
		};
		// Пишем также 0 перед каждой обычной моделью: состояние не переносится с выделенного куба.
		// Диапазон общий с gizmos, поэтому обновляем обе его стадии, даже если эффект читает только fragment.
		vkCmdPushConstants(command_buffer, pipeline_layout, LightGizmos::kPushConstantRange.stageFlags,
						   0, sizeof(effect), &effect);
		models[i].draw(command_buffer);
	}

	// Тот же pipeline layout — набор сцены (set 0) остаётся привязанным
	light_gizmos.record(command_buffer, lighting);
}

// Создаёт набор сцены, общий dynamic UBO и каталог с наборами материалов
bool Scene::createDescriptors() {
	auto& context = graphics::internal::context;

	// Набор сцены: UBO (binding 0), point/spot SSBO (1/2), карта теней солнца (3);
	// наборы материалов (set 1) создаются каталогом после выделения общего dynamic UBO
	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		kUniformBinding, storageBinding(1), storageBinding(2), kSunShadowBinding,
	};
	if (!createSetLayout(scene_bindings, scene_set_layout)) {
		return false;
	}

	// Один набор сцены; у каталога материалов отдельный пул.
	// Storage-буферы — массивы источников света в наборе сцены (точечные и прожекторы)
	const VkDescriptorPoolSize pool_sizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },
		{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
		{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
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

	const VkDescriptorImageInfo shadow_info = sun_shadow_map.comparisonDescriptor();
	const VkWriteDescriptorSet writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = scene_descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_info,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = scene_descriptor_set,
			.dstBinding = kSunShadowBinding.binding,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &shadow_info,
		},
	};

	vkUpdateDescriptorSets(context.device, uint32_t(std::size(writes)), writes, 0, nullptr);

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
	const VkDescriptorBufferInfo model_buffer_info = {
		.buffer = model_uniform_buffer.buffer,
		.offset = 0,
		.range = sizeof(ModelUniforms),
	};
	if (!materials.create(model_buffer_info)) {
		return false;
	}

	// Storage-буферы источников света сами привязываются к своим binding набора сцены
	return lighting.create(scene_descriptor_set);
}

// Создаёт CPU-состояние моделей; меши уже должны быть созданы
bool Scene::createModels() {
	// Первый куб: трансформация и анимация по орбите управляются из UI
	models.emplace_back(Model::Description{
		.name = "Cube",
		.mesh = &cube_mesh,
		.supports_albedo_effect = true,
	});

	// Второй куб: только вращение вокруг Y, без орбиты
	models.emplace_back(Model::Description{
		.name = "Second Cube",
		.mesh = &cube_mesh,
		.transform = { .position = glm::vec3(2.0f, 0.0f, 0.0f) },
		.animation = Animation(true, 0.0f, 0.0f, glm::vec3(0.0f, 30.0f, 0.0f)),
		.material_id = MaterialId::MaskedMetal,
	});

	models.emplace_back(Model::Description{
		.name = "Floor",
		.mesh = &plane_mesh,
		.transform = { .position = glm::vec3(0.0f, -1.0f, 0.0f), .scale = glm::vec3(12.0f, 1.0f, 12.0f) },
		.material = { .specular = glm::vec3(0.2f), .shininess = 16.0f, .uv_repeat = glm::vec2(6.0f) },
		.material_id = MaterialId::Tiles,
	});

	// У плоскости U идёт вдоль локальной Z, V — против X. Число повторов учитывает
	// длины этих осей после scale: один квадрат текстуры занимает 2×2 единицы мира.
	models.emplace_back(Model::Description{
		.name = "Back Wall",
		.mesh = &plane_mesh,
		.transform = { .position = glm::vec3(0.0f, 2.0f, -6.0f), .rotation_degrees = glm::vec3(90.0f, 0.0f, 0.0f),
					   .scale = glm::vec3(12.0f, 1.0f, 6.0f) },
		.material = { .specular = glm::vec3(0.1f), .shininess = 8.0f, .uv_repeat = glm::vec2(3.0f, 6.0f) },
		.material_id = MaterialId::Brick,
	});
	models.emplace_back(Model::Description{
		.name = "Left Wall",
		.mesh = &plane_mesh,
		.transform = { .position = glm::vec3(-6.0f, 2.0f, 0.0f), .rotation_degrees = glm::vec3(0.0f, 0.0f, -90.0f),
					   .scale = glm::vec3(6.0f, 1.0f, 12.0f) },
		.material = { .specular = glm::vec3(0.1f), .shininess = 8.0f, .uv_repeat = glm::vec2(6.0f, 3.0f) },
		.material_id = MaterialId::Brick,
	});

	// Общий с Second Cube материал: блики только на металлических участках карты.
	// Цвет/сила, shininess и переключатель specular независимы у каждой модели.
	models.emplace_back(Model::Description{
		.name = "Metal Cube",
		.mesh = &white_cube_mesh,
		.transform = { .position = glm::vec3(-3.0f, -0.5f, -3.0f) },
		.material = { .specular = glm::vec3(1.0f), .shininess = 256.0f },
		.material_id = MaterialId::MaskedMetal,
	});
	models.emplace_back(Model::Description{
		.name = "Matte Cube",
		.mesh = &white_cube_mesh,
		.transform = { .position = glm::vec3(0.0f, -0.5f, -4.0f) },
		.material = { .specular = glm::vec3(0.0f), .shininess = 1.0f },
		.material_id = MaterialId::Brick,
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
		.material = { .specular = glm::vec3(0.1f), .shininess = 8.0f, .uv_repeat = glm::vec2(2.0f) },
		.material_id = MaterialId::EmissivePanel,
	});

	if (models.size() > kMaxModels) {
		std::cerr << "Too many models in scene: " << models.size() << " > " << kMaxModels << '\n';
		return false;
	}
	shadow_draws.reserve(models.size());

	return true;
}

// Общий layout: set 0 — сцена, set 1 — материал; push constants [0, 80) — gizmos,
// а lit.frag использует только [0, 16) для эффекта albedo. Перед draw данные перезаписываются.
// Неосвещаемые шейдеры не используют набор материала; layout это допускает.
bool Scene::createPipeline() {
	const VkDescriptorSetLayout set_layouts[] = { scene_set_layout, materials.layout() };

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

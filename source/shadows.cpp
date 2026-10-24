#include "shadows.hpp"

#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include "graphics_internal.hpp"
#include "light_gizmos.hpp"
#include "lighting.hpp"
#include "pipeline.hpp"

namespace {

// Кандидаты в порядке предпочтения: 32-битная глубина точнее, 16-битная поддерживается везде
constexpr VkFormat kFormatCandidates[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM };

// Layout, в котором карты читаются в шейдере: в него их переводит render pass теней,
// с ним же должны быть записаны дескрипторы карт
constexpr VkImageLayout kSampledLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

// Привязки карт в наборе сцены (см. shaders/lighting.glsl)
constexpr uint32_t kDirectionalBinding = 3;
constexpr uint32_t kSpotBinding = 4;
constexpr uint32_t kPointBinding = 5;

// Ближняя плоскость граней кубической карты: геометрия ближе к источнику в карту не попадёт
constexpr float kPointShadowNear = 0.05f;

// Направления взгляда и оси «вверх» граней куба в порядке слоёв Vulkan: +X, −X, +Y, −Y, +Z, −Z.
// Оси «вверх» — из таблицы выборки кубических карт: на гранях ±X и ±Z координата t текстуры растёт
// вниз по −Y, на ±Y — вдоль ±Z. Связь NDC ↔ координаты текстуры в Vulkan та же, что в OpenGL,
// поэтому классическая таблица подходит без изменений, если не переворачивать Y в проекции
const glm::vec3 kCubeFaceDirections[] = {
	{ 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
	{ 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
};
const glm::vec3 kCubeFaceUps[] = {
	{ 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
	{ 0.0f, 0.0f, -1.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
};

// Матрица вида-проекции грани face куба вокруг position: квадратная перспектива 90° — 6 граней
// стыкуются без зазоров и перекрытий
glm::mat4 cubeFaceViewProjection(glm::vec3 position, uint32_t face) {
	const glm::mat4 view = glm::lookAt(position, position + kCubeFaceDirections[face], kCubeFaceUps[face]);
	const glm::mat4 projection =
		glm::perspective(glm::radians(90.0f), 1.0f, kPointShadowNear, Lighting::kPointShadowFar);
	return projection * view;
}

// Формат карты теней: в него нужно рисовать как в depth-привязку и читать его в шейдере.
// out_linear_filter сообщает, можно ли фильтровать его линейно
bool chooseFormat(VkFormat& out_format, bool& out_linear_filter) {
	constexpr VkFormatFeatureFlags kRequired =
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	for (VkFormat format : kFormatCandidates) {
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(graphics::internal::context.physical_device, format, &properties);

		// Image создаётся с VK_IMAGE_TILING_OPTIMAL, поэтому смотрим optimalTilingFeatures
		const VkFormatFeatureFlags features = properties.optimalTilingFeatures;
		if ((features & kRequired) == kRequired) {
			out_format = format;
			out_linear_filter = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
			return true;
		}
	}

	std::cerr << "No supported depth format for shadow maps\n";
	return false;
}

// View глубины на слои [base_layer, base_layer + layer_count) image
bool createDepthView(VkImage image, VkFormat format, VkImageViewType type, uint32_t base_layer,
					 uint32_t layer_count, VkImageView& out) {
	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = type,
		.format = format,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = base_layer,
			.layerCount = layer_count,
		},
	};

	if (vkCreateImageView(graphics::internal::context.device, &view_info, nullptr, &out) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan image view for shadow map\n";
		return false;
	}

	return true;
}

// Запись карты с сэмплером в binding набора; layout должен совпадать с finalLayout render pass теней
void writeMapDescriptor(VkDescriptorSet set, uint32_t binding, VkSampler sampler, VkImageView view) {
	const VkDescriptorImageInfo image_info = {
		.sampler = sampler,
		.imageView = view,
		.imageLayout = kSampledLayout,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image_info,
	};

	vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
}

} // namespace

bool Shadows::create() {
	return chooseFormat(format, linear_filter) && createSampler() && createRenderPass() &&
		   createMapArray(kDirectionalMapSize, 1, VK_IMAGE_VIEW_TYPE_2D, 0, directional_map) &&
		   createMapArray(kSpotMapSize, Lighting::kMaxShadowedSpotLights, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0,
						  spot_maps) &&
		   createMapArray(kPointMapSize, kCubeFaces * Lighting::kMaxShadowedPointLights,
						  VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, point_maps);
}

// Сэмплер со сравнением: texture() в шейдере возвращает не глубину, а результат
// «глубина точки <= глубины в карте», то есть 1 — освещено, 0 — в тени. Линейный фильтр
// усредняет результаты сравнения с 4 соседними текселями — мягкий край 2×2 бесплатно.
// За пределами карты — белая граница: глубина 1.0, точка считается освещённой
bool Shadows::createSampler() {
	const VkFilter filter = linear_filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	const VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = filter,
		.minFilter = filter,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.compareEnable = VK_TRUE,
		.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};

	if (vkCreateSampler(graphics::internal::context.device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan sampler for shadow map\n";
		return false;
	}

	return true;
}

// Один render pass на все карты: единственная привязка — глубина. Старое содержимое не нужно
// (UNDEFINED + CLEAR), после прохода слой сразу переходит в layout для чтения шейдером
bool Shadows::createRenderPass() {
	const VkAttachmentDescription attachment = {
		.format = format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = kSampledLayout,
	};

	const VkAttachmentReference depth_attachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	const VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 0,
		.pDepthStencilAttachment = &depth_attachment,
	};

	// Синхронизация с основным проходом, который читает карту во фрагментном шейдере:
	// до — запись глубины ждёт окончания чтения карты прошлым кадром,
	// после — чтение в основном проходе ждёт, пока глубина будет полностью записана
	const VkSubpassDependency dependencies[] = {
		{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
							VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
							 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		},
		{
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		},
	};

	const VkRenderPassCreateInfo render_pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = sizeof(dependencies) / sizeof(dependencies[0]),
		.pDependencies = dependencies,
	};

	if (vkCreateRenderPass(graphics::internal::context.device, &render_pass_info, nullptr, &render_pass) !=
		VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan render pass for shadow map\n";
		return false;
	}

	return true;
}

// Слой рисуется через свой 2D-view и framebuffer, а шейдер читает весь массив через sampled_view.
// SAMPLED в usage — карты читает фрагментный шейдер основного прохода. Для view типа CUBE_ARRAY
// image должен быть CUBE_COMPATIBLE, а число слоёв — кратно 6: каждые 6 слоёв — грани одного куба
bool Shadows::createMapArray(uint32_t size, uint32_t layer_count, VkImageViewType sampled_view_type,
							 VkImageCreateFlags image_flags, MapArray& out) const {
	auto& context = graphics::internal::context;

	out.size = size;
	out.layer_count = layer_count;

	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = image_flags,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { size, size, 1 },
		.mipLevels = 1,
		.arrayLayers = layer_count,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	if (vmaCreateImage(context.allocator, &image_info, &allocation_info, &out.image, &out.allocation, nullptr) !=
		VK_SUCCESS) {
		std::cerr << "Failed to allocate and create Vulkan image for shadow map\n";
		return false;
	}

	if (!createDepthView(out.image, format, sampled_view_type, 0, layer_count, out.sampled_view)) {
		return false;
	}

	out.layer_views.resize(layer_count, VK_NULL_HANDLE);
	out.framebuffers.resize(layer_count, VK_NULL_HANDLE);

	for (uint32_t layer = 0; layer < layer_count; ++layer) {
		if (!createDepthView(out.image, format, VK_IMAGE_VIEW_TYPE_2D, layer, 1, out.layer_views[layer])) {
			return false;
		}

		const VkFramebufferCreateInfo framebuffer_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = 1,
			.pAttachments = &out.layer_views[layer],
			.width = size,
			.height = size,
			.layers = 1,
		};

		if (vkCreateFramebuffer(context.device, &framebuffer_info, nullptr, &out.framebuffers[layer]) !=
			VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan framebuffer for shadow map layer " << layer << '\n';
			return false;
		}
	}

	return true;
}

void Shadows::destroyMapArray(MapArray& maps) const {
	auto& context = graphics::internal::context;

	for (VkFramebuffer framebuffer : maps.framebuffers) {
		vkDestroyFramebuffer(context.device, framebuffer, nullptr);
	}
	for (VkImageView view : maps.layer_views) {
		vkDestroyImageView(context.device, view, nullptr);
	}
	vkDestroyImageView(context.device, maps.sampled_view, nullptr);
	vmaDestroyImage(context.allocator, maps.image, maps.allocation);

	maps = {};
}

// Оба пайплайна пишут только глубину, без цветовых привязок. Отсечение граней выключено: пол и стены —
// односторонние плоскости, и отсечение одной из сторон выкинуло бы их из карты. У пайплайна кубических
// карт глубину пишет фрагментный шейдер (gl_FragDepth), поэтому depth bias растеризатора на неё не влияет
// и не включается — от «акне» там защищают только сдвиги в шейдере освещения
bool Shadows::createPipeline(VkPipelineLayout layout) {
	pipeline_layout = layout;

	return pipeline::create({
			   .vertex_shader = "shadow.vert.spv",
			   .fragment_shader = nullptr,
			   .layout = pipeline_layout,
			   .cull_mode = VK_CULL_MODE_NONE,
			   .render_pass = render_pass,
			   .color_attachment_count = 0,
			   .depth_bias = true,
		   }, pipeline) &&
		   pipeline::create({
			   .vertex_shader = "shadow_point.vert.spv",
			   .fragment_shader = "shadow_point.frag.spv",
			   .layout = pipeline_layout,
			   .cull_mode = VK_CULL_MODE_NONE,
			   .render_pass = render_pass,
			   .color_attachment_count = 0,
		   }, point_pipeline);
}

// Карты не пересоздаются, поэтому дескрипторы записываются один раз
void Shadows::bind(VkDescriptorSet scene_set) const {
	writeMapDescriptor(scene_set, kDirectionalBinding, sampler, directional_map.sampled_view);
	writeMapDescriptor(scene_set, kSpotBinding, sampler, spot_maps.sampled_view);
	writeMapDescriptor(scene_set, kPointBinding, sampler, point_maps.sampled_view);
}

void Shadows::upload(shader::SceneUniforms& uniforms) const {
	uniforms.shadow_params =
		glm::vec4(depth_bias_texels, normal_offset_texels, float(pcf_radius), enabled ? 1.0f : 0.0f);
	uniforms.shadow_debug = glm::uvec4(uint32_t(debug_view), uint32_t(debug_light), 0, 0);
}

// Проходы записываются каждый кадр и для каждого слоя, даже если тени выключены или источников меньше,
// чем слоёв: только render pass переводит слой в layout для чтения, а шейдер читает массив карт целиком
// (через один view), и все его слои должны быть в этом layout. Слой без источника просто очищается
void Shadows::record(VkCommandBuffer command_buffer, const std::optional<glm::mat4>& directional_view_projection,
					 std::span<const glm::mat4> spot_view_projections, std::span<const glm::vec3> point_positions,
					 std::span<const Model> models) const {
	if (directional_view_projection) {
		recordPass(command_buffer, directional_map.framebuffers[0], directional_map.size,
				   *directional_view_projection, models);
	} else {
		clearPass(command_buffer, directional_map.framebuffers[0], directional_map.size);
	}

	for (uint32_t layer = 0; layer < spot_maps.layer_count; ++layer) {
		if (layer < spot_view_projections.size()) {
			recordPass(command_buffer, spot_maps.framebuffers[layer], spot_maps.size, spot_view_projections[layer],
					   models);
		} else {
			clearPass(command_buffer, spot_maps.framebuffers[layer], spot_maps.size);
		}
	}

	for (uint32_t layer = 0; layer < point_maps.layer_count; ++layer) {
		const uint32_t light = layer / kCubeFaces;
		if (light < point_positions.size()) {
			recordPointFacePass(command_buffer, point_maps.framebuffers[layer], point_positions[light],
								layer % kCubeFaces, models);
		} else {
			clearPass(command_buffer, point_maps.framebuffers[layer], point_maps.size);
		}
	}
}

void Shadows::beginPass(VkCommandBuffer command_buffer, VkFramebuffer framebuffer, uint32_t size) const {
	const VkClearValue clear_value = { .depthStencil = { 1.0f, 0 } };

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = render_pass,
		.framebuffer = framebuffer,
		.renderArea = { .extent = { size, size } },
		.clearValueCount = 1,
		.pClearValues = &clear_value,
	};

	vkCmdBeginRenderPass(command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	// Viewport и scissor динамические — задаём под размер карты, а не окна
	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(size),
		.height = float(size),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = { .extent = { size, size } };
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

void Shadows::clearPass(VkCommandBuffer command_buffer, VkFramebuffer framebuffer, uint32_t size) const {
	beginPass(command_buffer, framebuffer, size);
	vkCmdEndRenderPass(command_buffer);
}

void Shadows::recordPass(VkCommandBuffer command_buffer, VkFramebuffer framebuffer, uint32_t size,
						 const glm::mat4& light_view_projection, std::span<const Model> models) const {
	beginPass(command_buffer, framebuffer, size);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdSetDepthBias(command_buffer, raster_bias_constant, 0.0f, raster_bias_slope);

	// Флаги стадий должны совпадать с диапазоном push constant в общем layout сцены
	const shader::ShadowPushConstants push = { .light_view_projection = light_view_projection };
	vkCmdPushConstants(command_buffer, pipeline_layout, LightGizmos::kPushConstantRange.stageFlags, 0,
					   sizeof(push), &push);

	drawModels(command_buffer, models);
	vkCmdEndRenderPass(command_buffer);
}

void Shadows::recordPointFacePass(VkCommandBuffer command_buffer, VkFramebuffer framebuffer,
								  glm::vec3 light_position, uint32_t face, std::span<const Model> models) const {
	beginPass(command_buffer, framebuffer, point_maps.size);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, point_pipeline);

	const shader::PointShadowPushConstants push = {
		.view_projection = cubeFaceViewProjection(light_position, face),
		.light = glm::vec4(light_position, Lighting::kPointShadowFar),
	};
	vkCmdPushConstants(command_buffer, pipeline_layout, LightGizmos::kPushConstantRange.stageFlags, 0,
					   sizeof(push), &push);

	drawModels(command_buffer, models);
	vkCmdEndRenderPass(command_buffer);
}

// Набор сцены (set 0) не нужен — шейдеры теней читают только матрицу модели из set 1.
// Модель без cast_shadows в карту не попадает: тень не отбрасывает, но тени на неё падают
void Shadows::drawModels(VkCommandBuffer command_buffer, std::span<const Model> models) const {
	for (const Model& model : models) {
		if (model.castsShadows()) {
			model.draw(command_buffer, pipeline_layout);
		}
	}
}

// Средства против «акне» (полос самозатенения): сдвиги точки к источнику и вдоль нормали в шейдере
// (в текселях карты) и depth bias растеризатора при рисовании карты. Слишком большие значения
// отрывают тень от основания объекта («peter panning»). PushID отделяет «Enabled» от чекбокса направленного света
void Shadows::drawUi() {
	ImGui::PushID("Shadows");
	ImGui::SeparatorText("Shadows");
	ImGui::Checkbox("Enabled", &enabled);
	ImGui::SliderFloat("Depth Bias (texels)", &depth_bias_texels, 0.0f, 5.0f);
	ImGui::SliderFloat("Normal Offset (texels)", &normal_offset_texels, 0.0f, 5.0f);
	ImGui::SliderFloat("Raster Bias Constant", &raster_bias_constant, 0.0f, 4.0f);
	ImGui::SliderFloat("Raster Bias Slope", &raster_bias_slope, 0.0f, 4.0f);
	// Ядро (2r + 1)²: 0 — жёсткий край, 1 — 3×3, 2 — 5×5, 3 — 7×7 (49 выборок на пиксель)
	ImGui::SliderInt("PCF Radius", &pcf_radius, 0, 3);

	// Тени есть только у первых источников каждого типа — индекс ограничен их числом
	const char* debug_views[] = { "Off", "Directional", "Spot Light", "Point Light" };
	ImGui::Combo("Debug View", &debug_view, debug_views, IM_ARRAYSIZE(debug_views));
	if (debug_view == kDebugSpot || debug_view == kDebugPoint) {
		const uint32_t shadowed = debug_view == kDebugSpot ? Lighting::kMaxShadowedSpotLights
														   : Lighting::kMaxShadowedPointLights;
		ImGui::SliderInt("Debug Light", &debug_light, 0, int(shadowed) - 1);
	}
	ImGui::PopID();
}

void Shadows::destroy() {
	auto& context = graphics::internal::context;

	vkDestroyPipeline(context.device, point_pipeline, nullptr);
	vkDestroyPipeline(context.device, pipeline, nullptr);

	destroyMapArray(point_maps);
	destroyMapArray(spot_maps);
	destroyMapArray(directional_map);
	vkDestroyRenderPass(context.device, render_pass, nullptr);
	vkDestroySampler(context.device, sampler, nullptr);
}

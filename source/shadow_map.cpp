#include "shadow_map.hpp"

#include <algorithm>
#include <iostream>

#include "graphics_internal.hpp"
#include "mesh.hpp"
#include "pipeline.hpp"
#include "shader_types.hpp"

namespace {

constexpr VkImageSubresourceRange kDepthRange = {
	.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
	.levelCount = 1,
	.layerCount = 1,
};
constexpr VkImageUsageFlags kImageUsage =
	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

} // namespace

bool ShadowMap::create() {
	if (!createImage() || !createSamplers() || !createDebugDescriptors() || !createPipelines()) {
		std::cerr << "Failed to create shadow map\n";
		destroy();
		return false;
	}
	return true;
}

void ShadowMap::destroy() {
	const auto& context = graphics::internal::context;
	vkDestroyPipeline(context.device, debug_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, debug_pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, debug_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, debug_set_layout, nullptr);
	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroySampler(context.device, debug_sampler, nullptr);
	vkDestroySampler(context.device, comparison_sampler, nullptr);
	vkDestroyImageView(context.device, view, nullptr);
	vmaDestroyImage(context.allocator, image, allocation);

	debug_pipeline = VK_NULL_HANDLE;
	debug_pipeline_layout = VK_NULL_HANDLE;
	debug_pool = VK_NULL_HANDLE;
	debug_set_layout = VK_NULL_HANDLE;
	debug_set = VK_NULL_HANDLE;
	pipeline = VK_NULL_HANDLE;
	pipeline_layout = VK_NULL_HANDLE;
	debug_sampler = VK_NULL_HANDLE;
	comparison_sampler = VK_NULL_HANDLE;
	view = VK_NULL_HANDLE;
	image = VK_NULL_HANDLE;
	allocation = VK_NULL_HANDLE;
	format = VK_FORMAT_UNDEFINED;
	readable = false;
}

VkDescriptorImageInfo ShadowMap::comparisonDescriptor() const {
	return {
		.sampler = comparison_sampler,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
}

bool ShadowMap::createImage() {
	const auto& context = graphics::internal::context;
	constexpr VkFormatFeatureFlags2 required = VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
	for (VkFormat candidate : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM }) {
		VkFormatProperties3 features = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
		VkFormatProperties2 properties = {
			.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
			.pNext = &features,
		};
		vkGetPhysicalDeviceFormatProperties2(context.physical_device, candidate, &properties);
		VkImageFormatProperties image_properties = {};
		if ((features.optimalTilingFeatures & required) == required &&
			vkGetPhysicalDeviceImageFormatProperties(context.physical_device, candidate, VK_IMAGE_TYPE_2D,
				VK_IMAGE_TILING_OPTIMAL, kImageUsage, 0, &image_properties) == VK_SUCCESS &&
			image_properties.maxExtent.width >= kResolution && image_properties.maxExtent.height >= kResolution) {
			format = candidate;
			break;
		}
	}
	if (format == VK_FORMAT_UNDEFINED) {
		std::cerr << "Device does not support a sampled 2048x2048 depth image with depth comparison\n";
		return false;
	}

	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { kResolution, kResolution, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = kImageUsage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	};
	if (vmaCreateImage(context.allocator, &image_info, &allocation_info, &image, &allocation, nullptr) != VK_SUCCESS) {
		return false;
	}
	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = kDepthRange,
	};
	return vkCreateImageView(context.device, &view_info, nullptr, &view) == VK_SUCCESS;
}

bool ShadowMap::createSamplers() {
	const auto& context = graphics::internal::context;
	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.compareEnable = VK_TRUE,
		.compareOp = VK_COMPARE_OP_LESS,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};
	if (vkCreateSampler(context.device, &sampler_info, nullptr, &comparison_sampler) != VK_SUCCESS) {
		return false;
	}
	// Для просмотра нужны сами значения глубины, а не результат сравнения.
	sampler_info.compareEnable = VK_FALSE;
	return vkCreateSampler(context.device, &sampler_info, nullptr, &debug_sampler) == VK_SUCCESS;
}

bool ShadowMap::createDebugDescriptors() {
	const auto& context = graphics::internal::context;
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};
	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &debug_set_layout) != VK_SUCCESS) {
		return false;
	}
	const VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &debug_pool) != VK_SUCCESS) {
		return false;
	}
	const VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = debug_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &debug_set_layout,
	};
	if (vkAllocateDescriptorSets(context.device, &allocate_info, &debug_set) != VK_SUCCESS) {
		return false;
	}
	const VkDescriptorImageInfo image_info = {
		.sampler = debug_sampler,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = debug_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image_info,
	};
	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	return true;
}

bool ShadowMap::createPipelines() {
	const VkPushConstantRange push_range = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.size = sizeof(shader::ShadowPushConstants),
	};
	if (!pipeline::createLayout({}, { &push_range, 1 }, pipeline_layout) ||
		!pipeline::create({
			.vertex_shader = "shadow.vert.spv",
			.fragment_shader = nullptr,
			.layout = pipeline_layout,
			// Пол и стены — плоскости: они должны попадать в карту с обеих сторон.
			.cull_mode = VK_CULL_MODE_NONE,
			.vertex_input = pipeline::VertexInput::PositionOnly,
			.dynamic_depth_format = format,
			.depth_bias = true,
		}, pipeline)) {
		return false;
	}
	return pipeline::createLayout({ &debug_set_layout, 1 }, {}, debug_pipeline_layout) &&
		pipeline::create({
			.vertex_shader = "depth_debug.vert.spv",
			.fragment_shader = "depth_debug.frag.spv",
			.layout = debug_pipeline_layout,
			.cull_mode = VK_CULL_MODE_NONE,
			.depth_write = false,
			.depth_test = false,
			.vertex_input = pipeline::VertexInput::None,
		}, debug_pipeline);
}

void ShadowMap::record(VkCommandBuffer command_buffer, const glm::mat4& light_view_projection,
					   std::span<const Draw> draws) {
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = readable ? VkAccessFlags(VK_ACCESS_SHADER_READ_BIT) : 0u,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.oldLayout = readable ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = kDepthRange,
	};
	constexpr VkPipelineStageFlags depth_stages =
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	vkCmdPipelineBarrier(command_buffer,
		readable ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		depth_stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	const VkRenderingAttachmentInfo attachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = { .depthStencil = { 1.0f, 0 } },
	};
	const VkRenderingInfo rendering_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { .extent = { kResolution, kResolution } },
		.layerCount = 1,
		.pDepthAttachment = &attachment,
	};
	vkCmdBeginRendering(command_buffer, &rendering_info);
	const VkViewport viewport = {
		.width = float(kResolution), .height = float(kResolution), .minDepth = 0.0f, .maxDepth = 1.0f,
	};
	const VkRect2D scissor = { .extent = { kResolution, kResolution } };
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	// Начальные параметры против самозатенения. Их управление через ImGui — в фазе 3.
	vkCmdSetDepthBias(command_buffer, 2.0f, 0.0f, 1.5f);
	for (const Draw& draw : draws) {
		const shader::ShadowPushConstants push = { .light_model = light_view_projection * draw.model };
		vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		draw.mesh->bind(command_buffer);
		draw.mesh->draw(command_buffer);
	}
	vkCmdEndRendering(command_buffer);

	barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(command_buffer, depth_stages, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);
	readable = true;
}

void ShadowMap::recordDebug(VkCommandBuffer command_buffer, VkExtent2D window_extent) const {
	const uint32_t side = std::min(window_extent.width, window_extent.height);
	const VkRect2D square = {
		.offset = { int32_t((window_extent.width - side) / 2), int32_t((window_extent.height - side) / 2) },
		.extent = { side, side },
	};
	const VkViewport viewport = {
		.x = float(square.offset.x), .y = float(square.offset.y),
		.width = float(side), .height = float(side), .minDepth = 0.0f, .maxDepth = 1.0f,
	};
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(command_buffer, 0, 1, &square);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_pipeline_layout,
		0, 1, &debug_set, 0, nullptr);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
}

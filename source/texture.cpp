#include "texture.hpp"

#include <cstring>
#include <iostream>
#include <vector>

#include <lodepng.h>

#include "graphics_internal.hpp"
#include "vk_buffer.hpp"
#include "vk_upload.hpp"

namespace {

constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkImageSubresourceRange kColorRange = {
	.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	.baseMipLevel = 0,
	.levelCount = 1,
	.baseArrayLayer = 0,
	.layerCount = 1,
};

} // namespace

bool Texture::create(const char* path) {
	std::vector<unsigned char> pixels;
	unsigned width = 0, height = 0;
	const unsigned error = lodepng::decode(pixels, width, height, path, LCT_RGBA, 8);
	if (error != 0) {
		std::cerr << "Failed to decode PNG '" << path << "': " << lodepng_error_text(error)
				  << " (" << error << ")\n";
		return false;
	}
	if (!createImage(width, height) || !upload(pixels, width, height) || !createViewAndSampler()) {
		std::cerr << "Failed to create GPU texture for PNG '" << path << "'\n";
		destroy();
		return false;
	}
	return true;
}

void Texture::destroy() {
	const auto& context = graphics::internal::context;
	vkDestroySampler(context.device, sampler, nullptr);
	vkDestroyImageView(context.device, view, nullptr);
	vmaDestroyImage(context.allocator, image, allocation);
	sampler = VK_NULL_HANDLE;
	view = VK_NULL_HANDLE;
	image = VK_NULL_HANDLE;
	allocation = VK_NULL_HANDLE;
}

VkDescriptorImageInfo Texture::descriptor() const {
	return {
		.sampler = sampler,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
}

bool Texture::createImage(uint32_t width, uint32_t height) {
	const auto& context = graphics::internal::context;
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(context.physical_device, &properties);
	if (width == 0 || height == 0 || width > properties.limits.maxImageDimension2D ||
		height > properties.limits.maxImageDimension2D) {
		std::cerr << "PNG dimensions exceed device limits: " << width << 'x' << height << '\n';
		return false;
	}
	VkFormatProperties format_properties;
	vkGetPhysicalDeviceFormatProperties(context.physical_device, kFormat, &format_properties);
	constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if ((format_properties.optimalTilingFeatures & required) != required) {
		std::cerr << "Device does not support linearly filtered RGBA8 sRGB textures\n";
		return false;
	}
	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = kFormat,
		.extent = { width, height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	};
	return vmaCreateImage(context.allocator, &image_info, &allocation_info, &image, &allocation, nullptr) ==
		VK_SUCCESS;
}

bool Texture::upload(std::span<const unsigned char> pixels, uint32_t width, uint32_t height) {
	vk_buffer::Buffer staging;
	if (!vk_buffer::create(pixels.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
		return false;
	}
	// lodepng хранит строки сверху вниз. UV (0, 0) соответствует верхнему левому углу PNG.
	std::memcpy(staging.mapped, pixels.data(), pixels.size_bytes());
	const bool uploaded = vk_buffer::flush(staging) && vk_upload::submit([&](VkCommandBuffer command_buffer) {
		VkImageMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = kColorRange,
		};
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 0, 0, nullptr, 0, nullptr, 1, &barrier);
		const VkBufferImageCopy copy = {
			.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
			.imageExtent = { width, height, 1 },
		};
		vkCmdCopyBufferToImage(command_buffer, staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							   1, &copy);
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
							 0, 0, nullptr, 0, nullptr, 1, &barrier);
	});
	vk_buffer::destroy(staging);
	return uploaded;
}

bool Texture::createViewAndSampler() {
	const auto& context = graphics::internal::context;
	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = kFormat,
		.subresourceRange = kColorRange,
	};
	if (vkCreateImageView(context.device, &view_info, nullptr, &view) != VK_SUCCESS) {
		return false;
	}
	const VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxLod = 0.0f,
	};
	return vkCreateSampler(context.device, &sampler_info, nullptr, &sampler) == VK_SUCCESS;
}

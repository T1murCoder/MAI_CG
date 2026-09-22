#pragma once

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace vk_buffer {

struct Buffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	void* mapped;
};

bool create(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out);
void destroy(Buffer& buffer);

} // namespace vk_buffer

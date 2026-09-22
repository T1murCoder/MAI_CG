#pragma once

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace vk_buffer {

// GPU-буфер, выделенный через VMA, с постоянно замапленным указателем на память
struct Buffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	void* mapped;
};

// Создаёт host-visible буфер размера size с флагами usage и мапит его в out.mapped
bool create(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out);
// Уничтожает буфер и освобождает его память через VMA
void destroy(Buffer& buffer);

} // namespace vk_buffer

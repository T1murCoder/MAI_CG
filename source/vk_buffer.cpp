#include "vk_buffer.hpp"

#include <iostream>

#include "graphics_internal.hpp"

namespace vk_buffer {

// Создаёт host-visible, постоянно замапленный буфер (без staging-буфера и копирования)
bool create(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	// Просим VMA сразу замапить память и оптимизировать её под последовательную запись с CPU
	const VmaAllocationCreateInfo allocation_create_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
				 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo allocation_info;

	if (vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &allocation_create_info,
						&out.buffer, &out.allocation, &allocation_info) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan buffer\n";
		return false;
	}

	// Запоминаем указатель на замапленную память, чтобы писать в буфер напрямую через memcpy
	out.mapped = allocation_info.pMappedData;

	return true;
}

// Уничтожает Vulkan-буфер и освобождает связанную с ним память VMA
void destroy(Buffer& buffer) {
	vmaDestroyBuffer(graphics::internal::context.allocator, buffer.buffer, buffer.allocation);
}

} // namespace vk_buffer

#include "vk_buffer.hpp"

#include <cstring>
#include <iostream>
#include <limits>

#include "graphics_internal.hpp"

namespace vk_buffer {

namespace {

// Временные ресурсы одной загрузки. Команды завершаются до выхода из upload().
struct UploadResources {
	Buffer staging;
	VkCommandPool pool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	~UploadResources() {
		const auto& context = graphics::internal::context;
		vkDestroyFence(context.device, fence, nullptr);
		vkDestroyCommandPool(context.device, pool, nullptr);
		destroy(staging);
	}
};

bool upload(std::span<const std::byte> data, VkBuffer destination) {
	const auto& context = graphics::internal::context;
	UploadResources resources;
	if (!create(data.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, resources.staging)) {
		return false;
	}
	std::memcpy(resources.staging.mapped, data.data(), data.size_bytes());
	if (!flush(resources.staging)) {
		return false;
	}

	const VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};
	if (vkCreateCommandPool(context.device, &pool_info, nullptr, &resources.pool) != VK_SUCCESS) {
		return false;
	}
	const VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = resources.pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	if (vkAllocateCommandBuffers(context.device, &allocate_info, &command_buffer) != VK_SUCCESS) {
		return false;
	}
	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
		return false;
	}
	const VkBufferCopy copy = { .size = data.size_bytes() };
	vkCmdCopyBuffer(command_buffer, resources.staging.buffer, destination, 1, &copy);

	// Зависимость transfer write -> последующее чтение (в том числе vertex/index input).
	const VkBufferMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = destination,
		.size = VK_WHOLE_SIZE,
	};
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						 0, 0, nullptr, 1, &barrier, 0, nullptr);
	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
		return false;
	}
	const VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	if (vkCreateFence(context.device, &fence_info, nullptr, &resources.fence) != VK_SUCCESS) {
		return false;
	}
	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffer,
	};
	if (vkQueueSubmit(context.graphics_queue, 1, &submit_info, resources.fence) != VK_SUCCESS) {
		return false;
	}
	if (vkWaitForFences(context.device, 1, &resources.fence, VK_TRUE,
						std::numeric_limits<uint64_t>::max()) != VK_SUCCESS) {
		// При ошибке ожидания не освобождаем ресурсы до остановки очереди.
		vkQueueWaitIdle(context.graphics_queue);
		return false;
	}
	return true;
}

} // namespace

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

bool createDeviceLocal(std::span<const std::byte> data, VkBufferUsageFlags usage, Buffer& out) {
	if (data.empty()) {
		std::cerr << "Cannot upload an empty Vulkan buffer\n";
		return false;
	}
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = data.size_bytes(),
		.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	};
	Buffer buffer;
	if (vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &allocation_info,
						&buffer.buffer, &buffer.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create device-local Vulkan buffer\n";
		return false;
	}
	if (!upload(data, buffer.buffer)) {
		std::cerr << "Failed to upload device-local Vulkan buffer\n";
		destroy(buffer);
		return false;
	}
	out = buffer;
	return true;
}

bool flush(const Buffer& buffer, VkDeviceSize offset, VkDeviceSize size) {
	if (vmaFlushAllocation(graphics::internal::context.allocator, buffer.allocation, offset, size) != VK_SUCCESS) {
		std::cerr << "Failed to flush Vulkan buffer\n";
		return false;
	}
	return true;
}

// Уничтожает Vulkan-буфер и освобождает связанную с ним память VMA
void destroy(Buffer& buffer) {
	vmaDestroyBuffer(graphics::internal::context.allocator, buffer.buffer, buffer.allocation);
	buffer = {};
}

} // namespace vk_buffer

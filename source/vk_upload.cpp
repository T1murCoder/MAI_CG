#include "vk_upload.hpp"

#include <iostream>
#include <limits>

#include "graphics_internal.hpp"

namespace vk_upload {

namespace {

struct Resources {
	VkCommandPool pool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	~Resources() {
		const auto& context = graphics::internal::context;
		vkDestroyFence(context.device, fence, nullptr);
		vkDestroyCommandPool(context.device, pool, nullptr);
	}
};

bool submitCommands(const std::function<void(VkCommandBuffer)>& record) {
	const auto& context = graphics::internal::context;
	Resources resources;
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
	record(command_buffer);
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
		vkQueueWaitIdle(context.graphics_queue);
		return false;
	}
	return true;
}

} // namespace

bool submit(const std::function<void(VkCommandBuffer)>& record) {
	if (!submitCommands(record)) {
		std::cerr << "Failed to submit or complete Vulkan resource upload\n";
		return false;
	}
	return true;
}

} // namespace vk_upload

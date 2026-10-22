#include "storage_array.hpp"

#include <algorithm>
#include <cstring>

#include "graphics_internal.hpp"

bool StorageArray::create(VkDeviceSize size, VkDescriptorSet set, uint32_t set_binding) {
	element_size = size;
	descriptor_set = set;
	binding = set_binding;
	return reallocate(kInitialCapacity);
}

void StorageArray::destroy() {
	if (capacity > 0) {
		vk_buffer::destroy(buffer);
	}
	capacity = 0;
}

uint32_t StorageArray::upload(const void* elements, uint32_t count) {
	if (count > capacity) {
		reallocate(std::max(count, 2 * capacity));
	}

	const uint32_t uploaded = std::min(count, capacity);
	if (uploaded > 0) {
		std::memcpy(buffer.mapped, elements, size_t(uploaded * element_size));
	}
	return uploaded;
}

// Буфер нулевого размера создать нельзя, поэтому ёмкость всегда хотя бы 1 — даже при пустом массиве
bool StorageArray::reallocate(uint32_t new_capacity) {
	new_capacity = std::max(new_capacity, 1u);

	vk_buffer::Buffer new_buffer;
	if (!vk_buffer::create(new_capacity * element_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, new_buffer)) {
		return false;
	}

	const VkDescriptorBufferInfo buffer_info = {
		.buffer = new_buffer.buffer,
		.offset = 0,
		.range = VK_WHOLE_SIZE,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor_set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &buffer_info,
	};

	vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);

	destroy();
	buffer = new_buffer;
	capacity = new_capacity;
	return true;
}

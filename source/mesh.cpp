#include "mesh.hpp"

#include <span>

bool Mesh::create(const geometry::MeshData& data) {
	if (!vk_buffer::createDeviceLocal(std::as_bytes(std::span(data.vertices)),
									  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer)) {
		return false;
	}
	if (!vk_buffer::createDeviceLocal(std::as_bytes(std::span(data.indices)),
									  VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		vk_buffer::destroy(vertex_buffer);
		return false;
	}

	index_count = uint32_t(data.indices.size());
	return true;
}

void Mesh::destroy() {
	vk_buffer::destroy(vertex_buffer);
	vk_buffer::destroy(index_buffer);
	index_count = 0;
}

void Mesh::bind(VkCommandBuffer command_buffer) const {
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer.buffer, &offset);
	vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
}

void Mesh::draw(VkCommandBuffer command_buffer) const {
	vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
}

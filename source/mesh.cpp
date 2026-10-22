#include "mesh.hpp"

#include <cstring>

bool Mesh::create(const geometry::MeshData& data) {
	const VkDeviceSize vertex_size = sizeof(data.vertices[0]) * data.vertices.size();
	if (!vk_buffer::create(vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer)) {
		return false;
	}
	std::memcpy(vertex_buffer.mapped, data.vertices.data(), size_t(vertex_size));

	const VkDeviceSize index_size = sizeof(data.indices[0]) * data.indices.size();
	if (!vk_buffer::create(index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		return false;
	}
	std::memcpy(index_buffer.mapped, data.indices.data(), size_t(index_size));

	index_count = uint32_t(data.indices.size());
	return true;
}

void Mesh::destroy() {
	vk_buffer::destroy(vertex_buffer);
	vk_buffer::destroy(index_buffer);
}

void Mesh::bind(VkCommandBuffer command_buffer) const {
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer.buffer, &offset);
	vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
}

void Mesh::draw(VkCommandBuffer command_buffer) const {
	vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
}

#include "mesh.hpp"

#include <span>

bool Mesh::create(const geometry::MeshData& data) {
	if (data.vertices.empty()) {
		return false;
	}
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
	glm::vec3 minimum = data.vertices.front().position;
	glm::vec3 maximum = minimum;
	for (const geometry::Vertex& vertex : data.vertices) {
		minimum = glm::min(minimum, vertex.position);
		maximum = glm::max(maximum, vertex.position);
	}
	for (size_t i = 0; i < local_bounds.size(); ++i) {
		local_bounds[i] = glm::vec3((i & 1) ? maximum.x : minimum.x,
			(i & 2) ? maximum.y : minimum.y, (i & 4) ? maximum.z : minimum.z);
	}
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

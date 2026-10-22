#include "geometry.hpp"

#include <cstddef>

namespace geometry {

namespace {

// Отображает локальную позицию вершины [-0.5, 0.5] в цвет [0, 1] по каждой оси
glm::vec3 colorFromPosition(glm::vec3 position) {
	return position + glm::vec3(0.5f);
}

// Грань куба: внешняя нормаль n и две оси в плоскости грани, причём cross(u, v) == n
struct CubeFace {
	glm::vec3 n;
	glm::vec3 u;
	glm::vec3 v;
};

constexpr CubeFace kCubeFaces[] = {
	{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // -Z
	{ { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },  // +Z
	{ { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } }, // -X
	{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },  // +X
	{ { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // -Y
	{ { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },  // +Y
};

} // namespace

// Каждая грань строится из своих 4 вершин с общей нормалью грани. Углы идут в порядке
// (-u-v), (+u-v), (+u+v), (-u+v): для треугольника (0, 1, 2) cross(c1 - c0, c2 - c0) ~ cross(u, v) == n,
// то есть обход против часовой стрелки при взгляде снаружи. Цвет вершины выводится из её позиции
MeshData makeCube() {
	MeshData data;
	data.vertices.reserve(24);
	data.indices.reserve(36);

	const glm::vec2 corners[] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };

	for (const CubeFace& face : kCubeFaces) {
		const uint16_t base = uint16_t(data.vertices.size());

		for (const glm::vec2& corner : corners) {
			const glm::vec3 position = 0.5f * (face.n + corner.x * face.u + corner.y * face.v);
			data.vertices.push_back({ position, colorFromPosition(position), face.n });
		}

		for (const uint16_t index : { 0, 1, 2, 0, 2, 3 }) {
			data.indices.push_back(uint16_t(base + index));
		}
	}

	return data;
}

// GPU будет сдвигать указатель в буфере после чтения каждой вершины
VkVertexInputBindingDescription vertexBinding() {
	return {
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
}

std::array<VkVertexInputAttributeDescription, 3> vertexAttributes() {
	return { {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, color),
		},
		{
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, normal),
		},
	} };
}

} // namespace geometry

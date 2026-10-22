#include "geometry.hpp"

#include <cstddef>

namespace geometry {

namespace {

// Отображает локальную позицию вершины [-0.5, 0.5] в цвет [0, 1] по каждой оси
glm::vec3 colorFromPosition(glm::vec3 position) {
	return position + glm::vec3(0.5f);
}

// Белый цвет вершины — поверхность окрашивается только альбедо материала
glm::vec3 whiteColor(glm::vec3) {
	return glm::vec3(1.0f);
}

// Ориентация квадрата: внешняя нормаль n и две оси в его плоскости, причём cross(u, v) == n
struct QuadBasis {
	glm::vec3 n;
	glm::vec3 u;
	glm::vec3 v;
};

constexpr QuadBasis kUpBasis = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } };

constexpr QuadBasis kCubeFaces[] = {
	{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // -Z
	{ { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },  // +Z
	{ { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } }, // -X
	{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },  // +X
	{ { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // -Y
	kUpBasis,                                                              // +Y
};

// Добавляет квадрат со стороной 1 с центром center: 4 вершины с общей нормалью и 2 треугольника.
// Углы идут в порядке (-u-v), (+u-v), (+u+v), (-u+v): для треугольника (0, 1, 2)
// cross(c1 - c0, c2 - c0) ~ cross(u, v) == n, то есть обход против часовой стрелки со стороны нормали
void appendQuad(MeshData& data, glm::vec3 center, const QuadBasis& basis, glm::vec3 (*color)(glm::vec3)) {
	const glm::vec2 corners[] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
	const uint16_t base = uint16_t(data.vertices.size());

	for (const glm::vec2& corner : corners) {
		const glm::vec3 position = center + 0.5f * (corner.x * basis.u + corner.y * basis.v);
		data.vertices.push_back({ position, color(position), basis.n });
	}

	for (const uint16_t index : { 0, 1, 2, 0, 2, 3 }) {
		data.indices.push_back(uint16_t(base + index));
	}
}

} // namespace

// Каждая грань — отдельный квадрат, сдвинутый от центра на половину ребра вдоль своей нормали;
// цвет вершины выводится из её позиции
MeshData makeCube() {
	MeshData data;
	data.vertices.reserve(24);
	data.indices.reserve(36);

	for (const QuadBasis& face : kCubeFaces) {
		appendQuad(data, 0.5f * face.n, face, colorFromPosition);
	}

	return data;
}

MeshData makePlane() {
	MeshData data;
	appendQuad(data, glm::vec3(0.0f), kUpBasis, whiteColor);
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

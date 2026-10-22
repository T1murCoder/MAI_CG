#include "geometry.hpp"

#include <cstddef>

namespace geometry {

namespace {

// Отображает локальную позицию вершины [-0.5, 0.5] в цвет [0, 1] по каждой оси
glm::vec3 colorFromPosition(glm::vec3 position) {
	return position + glm::vec3(0.5f);
}

} // namespace

// Углы единичного куба; цвет каждой вершины процедурно выводится из её позиции
MeshData makeCube() {
	MeshData data;

	data.vertices = {
		{ { -0.5f, -0.5f, -0.5f }, colorFromPosition({ -0.5f, -0.5f, -0.5f }) }, // 0
		{ { 0.5f, -0.5f, -0.5f }, colorFromPosition({ 0.5f, -0.5f, -0.5f }) },   // 1
		{ { -0.5f, 0.5f, -0.5f }, colorFromPosition({ -0.5f, 0.5f, -0.5f }) },   // 2
		{ { 0.5f, 0.5f, -0.5f }, colorFromPosition({ 0.5f, 0.5f, -0.5f }) },     // 3
		{ { -0.5f, -0.5f, 0.5f }, colorFromPosition({ -0.5f, -0.5f, 0.5f }) },   // 4
		{ { 0.5f, -0.5f, 0.5f }, colorFromPosition({ 0.5f, -0.5f, 0.5f }) },     // 5
		{ { -0.5f, 0.5f, 0.5f }, colorFromPosition({ -0.5f, 0.5f, 0.5f }) },     // 6
		{ { 0.5f, 0.5f, 0.5f }, colorFromPosition({ 0.5f, 0.5f, 0.5f }) },       // 7
	};

	// Индексы граней: по 6 индексов (2 треугольника) на каждую из 6 граней куба
	data.indices = {
		// -Z
		0, 2, 3, 0, 3, 1,
		// +Z
		4, 5, 7, 4, 7, 6,
		// -X
		0, 4, 6, 0, 6, 2,
		// +X
		1, 3, 7, 1, 7, 5,
		// -Y
		0, 1, 5, 0, 5, 4,
		// +Y
		2, 6, 7, 2, 7, 3,
	};

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

std::array<VkVertexInputAttributeDescription, 2> vertexAttributes() {
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
	} };
}

} // namespace geometry

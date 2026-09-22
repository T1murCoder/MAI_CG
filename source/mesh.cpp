#include "mesh.hpp"

namespace mesh {

namespace {

// Отображает локальную позицию вершины [-0.5, 0.5] в цвет [0, 1] по каждой оси
glm::vec3 colorFromPosition(glm::vec3 position) {
	return position + glm::vec3(0.5f);
}

} // namespace

// Углы единичного куба; цвет каждой вершины процедурно выводится из её позиции
const std::array<Vertex, 8> cube_vertices = { {
	{ { -0.5f, -0.5f, -0.5f }, colorFromPosition({ -0.5f, -0.5f, -0.5f }) }, // 0
	{ { 0.5f, -0.5f, -0.5f }, colorFromPosition({ 0.5f, -0.5f, -0.5f }) },   // 1
	{ { -0.5f, 0.5f, -0.5f }, colorFromPosition({ -0.5f, 0.5f, -0.5f }) },   // 2
	{ { 0.5f, 0.5f, -0.5f }, colorFromPosition({ 0.5f, 0.5f, -0.5f }) },     // 3
	{ { -0.5f, -0.5f, 0.5f }, colorFromPosition({ -0.5f, -0.5f, 0.5f }) },   // 4
	{ { 0.5f, -0.5f, 0.5f }, colorFromPosition({ 0.5f, -0.5f, 0.5f }) },     // 5
	{ { -0.5f, 0.5f, 0.5f }, colorFromPosition({ -0.5f, 0.5f, 0.5f }) },     // 6
	{ { 0.5f, 0.5f, 0.5f }, colorFromPosition({ 0.5f, 0.5f, 0.5f }) },       // 7
} };

// Индексы граней: по 6 индексов (2 треугольника) на каждую из 6 граней куба
const std::array<uint16_t, 36> cube_indices = {
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

} // namespace mesh

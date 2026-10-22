#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <glm/glm.hpp>

namespace geometry {

// Вершина меша: позиция в локальных координатах и процедурно вычисленный цвет
struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};

// Геометрия меша в памяти CPU: вершины и индексы треугольников (обход CCW снаружи)
struct MeshData {
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
};

// Единичный куб с центром в начале координат: 8 вершин-углов и 36 индексов (по 2 треугольника на грань)
MeshData makeCube();

// Описание вершинного буфера для пайплайна: один буфер в binding 0, шаг — одна Vertex
VkVertexInputBindingDescription vertexBinding();

// Описание атрибутов Vertex для пайплайна: позиция (location 0) и цвет (location 1)
std::array<VkVertexInputAttributeDescription, 2> vertexAttributes();

} // namespace geometry

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <glm/glm.hpp>

namespace geometry {

// Вершина меша: позиция в локальных координатах, процедурно вычисленный цвет и нормаль поверхности
struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
	glm::vec3 normal;
};

// Геометрия меша в памяти CPU: вершины и индексы треугольников (обход CCW снаружи)
struct MeshData {
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
};

// Единичный куб с центром в начале координат: 24 вершины (по 4 на грань, чтобы у каждой грани
// была своя нормаль) и 36 индексов (по 2 треугольника на грань)
MeshData makeCube();

// Квадрат 1×1 в плоскости XZ с центром в начале координат, нормаль +Y (лицевая сторона смотрит вверх);
// цвет вершин белый — поверхность окрашивается только материалом
MeshData makePlane();

// Описание вершинного буфера для пайплайна: один буфер в binding 0, шаг — одна Vertex
VkVertexInputBindingDescription vertexBinding();

// Описание атрибутов Vertex для пайплайна: позиция (location 0), цвет (location 1), нормаль (location 2)
std::array<VkVertexInputAttributeDescription, 3> vertexAttributes();

} // namespace geometry

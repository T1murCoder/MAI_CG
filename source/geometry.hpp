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
// была своя нормаль) и 36 индексов (по 2 треугольника на грань). С vertex_colors цвет вершины
// выводится из её позиции (радужный куб из ЛР1), иначе вершины белые и цвет задаёт только материал
MeshData makeCube(bool vertex_colors = true);

// Квадрат 1×1 в плоскости XZ с центром в начале координат, нормаль +Y (лицевая сторона смотрит вверх);
// цвет вершин белый — поверхность окрашивается только материалом
MeshData makePlane();

// Каркас единичного конуса для рисования линиями (LINE_LIST, индексы парами): вершина в начале
// координат, ось +Z, основание — окружность радиуса 1 в плоскости z = 1 из segments отрезков.
// Кроме окружности — ось конуса и 4 образующие; segments должно делиться на 4
MeshData makeConeLines(uint32_t segments);

// Описание вершинного буфера для пайплайна: один буфер в binding 0, шаг — одна Vertex
VkVertexInputBindingDescription vertexBinding();

// Описание атрибутов Vertex для пайплайна: позиция (location 0), цвет (location 1), нормаль (location 2)
std::array<VkVertexInputAttributeDescription, 3> vertexAttributes();

} // namespace geometry

#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace mesh {

// Вершина куба: позиция в локальных координатах и процедурно вычисленный цвет
struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};

// 8 уникальных вершин-углов куба; цвет каждой вычисляется из позиции (см. mesh.cpp)
extern const std::array<Vertex, 8> cube_vertices;

// 36 индексов (12 треугольников, по 2 на каждую из 6 граней), обход CCW снаружи
extern const std::array<uint16_t, 36> cube_indices;

} // namespace mesh

#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace mesh {

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};


extern const std::array<Vertex, 8> cube_vertices;

extern const std::array<uint16_t, 36> cube_indices;

} // namespace mesh

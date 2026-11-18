#pragma once

#include <span>

#include <glm/glm.hpp>

namespace sun_shadow {

struct Area {
	static constexpr float kMinSize = 0.5f;
	static constexpr float kMaxSize = 100.0f;
	static constexpr float kMaxCenter = 1000.0f;

	glm::vec3 center = glm::vec3(0.0f, 1.0f, -1.0f);
	float size = 20.0f; // сторона квадратной области в мировых единицах

	void sanitize();
};

struct Projection {
	glm::mat4 view_projection = glm::mat4(1.0f);
	double near_plane = 0.1;
	double far_plane = 40.0;
};

// XY задаются областью в мире, Z подбирается по границам моделей с запасом.
// Камера не участвует в расчёте; нулевое направление заменяется направлением вниз.
Projection fit(const Area& area, glm::vec3 direction, std::span<const glm::vec3> world_bounds);

} // namespace sun_shadow

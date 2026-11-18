#include "sun_shadow.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace sun_shadow {

void Area::sanitize() {
	const Area defaults;
	for (int axis = 0; axis < 3; ++axis) {
		center[axis] = std::isfinite(center[axis]) ? std::clamp(center[axis], -kMaxCenter, kMaxCenter) :
			defaults.center[axis];
	}
	size = std::isfinite(size) ? std::clamp(size, kMinSize, kMaxSize) : defaults.size;
}

Projection fit(const Area& requested_area, glm::vec3 direction, std::span<const glm::vec3> world_bounds) {
	Area area = requested_area;
	area.sanitize();
	// Double защищает нормализацию и подбор глубины от переполнения/потери малых значений.
	glm::dvec3 ray(direction);
	const double length_squared = glm::dot(ray, ray);
	ray = std::isfinite(length_squared) && length_squared > 0.0 ? ray / std::sqrt(length_squared) :
		glm::dvec3(0.0, -1.0, 0.0);
	const glm::dvec3 center(area.center);

	double nearest = std::numeric_limits<double>::infinity();
	double farthest = -std::numeric_limits<double>::infinity();
	for (const glm::vec3& point : world_bounds) {
		const double distance = glm::dot(ray, glm::dvec3(point) - center);
		if (std::isfinite(distance)) {
			nearest = std::min(nearest, distance);
			farthest = std::max(farthest, distance);
		}
	}
	if (!std::isfinite(nearest)) {
		nearest = farthest = 0.0;
	}
	// Запас сохраняет крайние поверхности внутри depth range, в том числе при анимации и bias.
	const double padding = std::max(1.0, 0.05 * (farthest - nearest));
	Projection result;
	const double eye_distance = result.near_plane + padding - nearest;
	result.far_plane = result.near_plane + (farthest - nearest) + 2.0 * padding;

	const glm::dvec3 up = std::abs(ray.y) > 0.99 ? glm::dvec3(0.0, 0.0, 1.0) : glm::dvec3(0.0, 1.0, 0.0);
	// Сначала ориентация, затем перенос: не вычитаем близкие eye/target при большом центре.
	glm::dmat4 view = glm::lookAt(glm::dvec3(0.0), ray, up) *
		glm::translate(glm::dmat4(1.0), -center);
	view[3].z -= eye_distance;
	const double half_size = 0.5 * area.size;
	glm::dmat4 projection = glm::ortho(-half_size, half_size, -half_size, half_size,
		result.near_plane, result.far_plane);
	// GLM_FORCE_DEPTH_ZERO_TO_ONE, как у камеры; viewport имеет положительную высоту.
	projection[1][1] *= -1.0;
	result.view_projection = glm::mat4(projection * view);
	return result;
}

} // namespace sun_shadow

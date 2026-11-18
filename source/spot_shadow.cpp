#include "spot_shadow.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace spot_shadow {

std::optional<Projection> fit(const shader::SpotLight& light, std::span<const glm::vec3> world_bounds) {
	const glm::dvec3 position(light.position);
	for (int axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(position[axis])) {
			return std::nullopt;
		}
	}
	glm::dvec3 ray(light.direction);
	const double length_squared = glm::dot(ray, ray);
	ray = std::isfinite(length_squared) && length_squared > 0.0 ? ray / std::sqrt(length_squared) :
		glm::dvec3(0.0, -1.0, 0.0);
	double farthest = 0.0;
	for (const glm::vec3& point : world_bounds) {
		const double distance = glm::dot(ray, glm::dvec3(point) - position);
		if (std::isfinite(distance)) {
			farthest = std::max(farthest, distance);
		}
	}
	Projection result;
	result.far_plane = std::max(1.0, farthest + std::max(1.0, 0.05 * farthest));
	if (result.far_plane > std::numeric_limits<float>::max()) {
		return std::nullopt;
	}
	const double outer_cosine = std::isfinite(light.cutoff.y) ? light.cutoff.y : std::cos(glm::radians(25.0));
	const double half_angle = std::acos(std::clamp(outer_cosine,
		std::cos(glm::radians(89.0)), std::cos(glm::radians(1.0))));
	const glm::dvec3 up = std::abs(ray.y) > 0.99 ? glm::dvec3(0.0, 0.0, 1.0) : glm::dvec3(0.0, 1.0, 0.0);
	const glm::dmat4 view = glm::lookAt(glm::dvec3(0.0), ray, up) *
		glm::translate(glm::dmat4(1.0), -position);
	// outer angle — половина угла конуса; карта квадратная, поэтому aspect = 1.
	glm::dmat4 projection = glm::perspective(2.0 * half_angle, 1.0, result.near_plane, result.far_plane);
	projection[1][1] *= -1.0;
	result.view_projection = glm::mat4(projection * view);
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			if (!std::isfinite(result.view_projection[column][row])) {
				return std::nullopt;
			}
		}
	}
	return result;
}

} // namespace spot_shadow

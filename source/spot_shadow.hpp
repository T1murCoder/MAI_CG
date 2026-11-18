#pragma once

#include <optional>
#include <span>

#include "shader_types.hpp"

namespace spot_shadow {

struct Projection {
	glm::mat4 view_projection = glm::mat4(1.0f);
	double near_plane = 0.1;
	double far_plane = 40.0;
};

// Положение, направление и внешний конус берутся из загруженного снимка источника.
// При некорректном положении или непредставимой матрице генерация карты отключается.
std::optional<Projection> fit(const shader::SpotLight& light, std::span<const glm::vec3> world_bounds);

} // namespace spot_shadow

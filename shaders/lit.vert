#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUv;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPosition;

// UV, нормаль и мировая позиция интерполируются по треугольнику
void main() {
	const vec4 world_position = object.model * vec4(inPosition, 1.0);
	gl_Position = scene.view_projection * world_position;

	fragUv = inUv;
	fragNormal = mat3(object.normal_matrix) * inNormal;
	fragWorldPosition = world_position.xyz;
}

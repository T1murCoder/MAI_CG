#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"
#include "shadow_point.glsl"

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragWorldPosition;

// Переводит вершину модели в clip space грани куба; мировая позиция нужна фрагментному шейдеру,
// чтобы записать расстояние до источника
void main() {
	const vec4 world_position = object.model * vec4(inPosition, 1.0);
	gl_Position = point_shadow.view_projection * world_position;
	fragWorldPosition = world_position.xyz;
}

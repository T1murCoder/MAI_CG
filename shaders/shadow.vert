#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"
#include "shadow.glsl"

layout(location = 0) in vec3 inPosition;

// Переводит вершину модели в clip space источника света. Фрагментного шейдера у прохода нет:
// растеризатор сам записывает глубину в карту теней
void main() {
	gl_Position = shadow.light_view_projection * object.model * vec4(inPosition, 1.0);
}

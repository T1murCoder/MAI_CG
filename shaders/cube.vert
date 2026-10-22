#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

// Переводит вершину в clip space и передаёт её цвет дальше на интерполяцию по треугольнику
void main() {
	gl_Position = scene.view_projection * object.model * vec4(inPosition, 1.0);
	fragColor = inColor;
}

#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

// Итоговый цвет пикселя = интерполированный процедурный цвет вершин, умноженный на цвет модели
void main() {
	outColor = vec4(fragColor, 1.0) * object.color;
}

#version 450
#extension GL_GOOGLE_include_directive : require

#include "gizmo.glsl"

layout(location = 0) out vec4 outColor;

// Постоянный цвет без освещения: фигура выглядит одинаково при любом свете в сцене
void main() {
	outColor = vec4(gizmo.color.rgb, 1.0);
}

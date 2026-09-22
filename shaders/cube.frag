#version 450

// Цвет, выбранный в UI (ColorEdit4), общий для всех вершин текущего объекта
layout(push_constant) uniform PushConstants {
	vec4 color;
} pc;

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

// Итоговый цвет пикселя = интерполированный процедурный цвет вершин, умноженный на цвет из UI
void main() {
	outColor = vec4(fragColor, 1.0) * pc.color;
}

#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"
#include "gizmo.glsl"

// Из вершины нужна только позиция; цвет и нормаль в формате вершины есть, но неосвещаемой фигуре не нужны
layout(location = 0) in vec3 inPosition;

// Переводит вершину фигуры-визуализации в clip space той же камерой, что и остальную сцену
void main() {
	gl_Position = scene.view_projection * gizmo.model * vec4(inPosition, 1.0);
}

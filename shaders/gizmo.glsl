#ifndef GIZMO_GLSL
#define GIZMO_GLSL

// Push constant неосвещаемых фигур-визуализаций источников света. Это контракт с C++:
// раскладка должна совпадать с shader::GizmoPushConstants в source/shader_types.hpp
layout(push_constant) uniform GizmoPushConstants {
	mat4 model;
	vec4 color; // rgb — постоянный цвет фигуры
} gizmo;

#endif // GIZMO_GLSL

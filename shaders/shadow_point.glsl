#ifndef SHADOW_POINT_GLSL
#define SHADOW_POINT_GLSL

// Push constant прохода теней точечного источника (одна грань кубической карты за проход). Это контракт
// с C++: раскладка должна совпадать с shader::PointShadowPushConstants в source/shader_types.hpp.
// Занимает тот же диапазон push constant общего pipeline layout, что и GizmoPushConstants
layout(push_constant) uniform PointShadowPushConstants {
	mat4 view_projection; // мир → clip space грани куба
	vec4 light;           // xyz — положение источника в мире, w — дальность карты (far)
} point_shadow;

#endif // SHADOW_POINT_GLSL

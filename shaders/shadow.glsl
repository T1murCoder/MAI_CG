#ifndef SHADOW_GLSL
#define SHADOW_GLSL

// Push constant прохода теней. Это контракт с C++: раскладка должна совпадать
// с shader::ShadowPushConstants в source/shader_types.hpp. Занимает начало того же диапазона
// push constant общего pipeline layout, что и GizmoPushConstants
layout(push_constant) uniform ShadowPushConstants {
	mat4 light_view_projection; // мир → clip space источника, с точки зрения которого рисуется карта
} shadow;

#endif // SHADOW_GLSL

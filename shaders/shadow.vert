#version 450

layout(location = 0) in vec3 inPosition;

// Контракт с shader::ShadowPushConstants. Материалы для записи глубины не нужны.
layout(push_constant) uniform ShadowPushConstants {
	mat4 light_model;
} shadow;

void main() {
	gl_Position = shadow.light_model * vec4(inPosition, 1.0);
}

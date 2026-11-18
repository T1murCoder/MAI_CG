#version 450

layout(set = 0, binding = 0) uniform sampler2D depth_map;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
	// У ортографической проекции глубина уже линейна: чёрный — близко, белый — далеко.
	float depth = texture(depth_map, inUV).r;
	outColor = vec4(vec3(depth), 1.0);
}

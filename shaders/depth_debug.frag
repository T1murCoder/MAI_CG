#version 450

layout(set = 0, binding = 0) uniform sampler2D depth_map;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// Контракт с shader::DepthDebugPushConstants.
layout(push_constant) uniform DepthDebugPushConstants {
	vec4 depth_range;
} debug_view;

void main() {
	float depth = texture(depth_map, inUV).r;
	if (debug_view.depth_range.z > 0.5) {
		// Линейная глубина, нормированная между near и far, для Vulkan Z в [0, 1].
		// Такая форма сохраняет depth = 1 для пустых текселей без вычитания близких чисел.
		float near_depth = debug_view.depth_range.x * depth;
		depth = near_depth / (debug_view.depth_range.y * (1.0 - depth) + near_depth);
	}
	// У ортографической карты глубина уже линейна. Чёрный — близко, белый — далеко.
	outColor = vec4(vec3(depth), 1.0);
}

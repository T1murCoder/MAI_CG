#version 450

// MVP-матрицы текущего объекта из его собственного uniform-буфера (свой descriptor set на куб)
layout(binding = 0) uniform UBO {
	mat4 model;
	mat4 view;
	mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

// Переводит вершину в clip space и передаёт её цвет дальше на интерполяцию по треугольнику
void main() {
	gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
	fragColor = inColor;
}

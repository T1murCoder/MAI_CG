#version 450

layout(location = 0) out vec2 outUV;

// Один fullscreen-треугольник без вершинного буфера. UV (0, 0) — верх карты.
void main() {
	vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	outUV = position;
	gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}

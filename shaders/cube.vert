#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;

// Переводит вершину в clip space и передаёт её цвет и нормаль дальше на интерполяцию по треугольнику
void main() {
	gl_Position = scene.view_projection * object.model * vec4(inPosition, 1.0);
	fragColor = inColor;
	// Нормаль в мировых координатах; при неравномерном масштабе понадобится обратная транспонированная матрица
	fragNormal = mat3(object.model) * inNormal;
}

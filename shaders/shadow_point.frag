#version 450
#extension GL_GOOGLE_include_directive : require

#include "shadow_point.glsl"

layout(location = 0) in vec3 fragWorldPosition;

// Вместо перспективной глубины в карту пишется расстояние до источника, делённое на дальность карты:
// при выборке его можно сравнить с length(точка − источник) / far напрямую, с какой бы грани куба ни
// пришлась точка. Тест глубины по-прежнему оставляет ближайшую к источнику поверхность — вдоль луча
// из источника расстояние растёт так же монотонно, как глубина. Значение ограничено 1: углы грани
// дальше far, чем её центр
void main() {
	gl_FragDepth = min(length(fragWorldPosition - point_shadow.light.xyz) / point_shadow.light.w, 1.0);
}

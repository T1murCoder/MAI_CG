#pragma once

#include <glm/glm.hpp>

#include "shader_types.hpp"

// Рассеянный свет: приходит отовсюду одинаково, освещает все поверхности без учёта нормали
struct AmbientLight {
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 0.15f;

	void drawUi();
};

// Направленный источник: бесконечно удалённый, все лучи параллельны (как солнце)
struct DirectionalLight {
	bool enabled = true;
	glm::vec3 direction = glm::vec3(-0.4f, -1.0f, -0.6f); // куда летят лучи; нормализуется при упаковке
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;

	void drawUi();
};

// Все источники света сцены: хранит их параметры на CPU, рисует для них UI
// и упаковывает в формат GPU
class Lighting {
public:
	// Записывает параметры рассеянного и направленного света в uniform-данные сцены
	void fill(shader::SceneUniforms& uniforms) const;

	// Секции всех источников света в текущем окне ImGui
	void drawUi();

private:
	AmbientLight ambient;
	DirectionalLight directional;
};

#pragma once

#include <vector>

#include <vulkan/vulkan_core.h>

#include <glm/glm.hpp>

#include "animation.hpp"
#include "shader_types.hpp"
#include "storage_array.hpp"

// Нормализует направление источника; нулевой вектор нормализовать нельзя — тогда «сверху вниз»
glm::vec3 normalizeDirection(glm::vec3 direction);

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

// Точечный источник: светит из точки во все стороны, яркость падает как 1/d².
// Анимация может водить источник по орбите вокруг position
struct PointLight {
	glm::vec3 position = glm::vec3(0.0f, 2.0f, 0.0f);
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 5.0f;
	Animation animation;

	// Положение с учётом анимации — именно оно уходит в шейдер и в визуализацию
	glm::vec3 worldPosition() const;

	void drawUi();
};

// Прожектор: точечный источник, светящий только внутри конуса вокруг direction. Внутри inner-угла
// светит в полную силу, между inner и outer плавно гаснет (мягкий край), снаружи outer — не светит.
// Анимация может водить прожектор по орбите и вращать его направление
struct SpotLight {
	glm::vec3 position = glm::vec3(0.0f, 3.0f, 0.0f);
	glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f); // ось конуса; нормализуется при упаковке
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 20.0f;
	float inner_degrees = 15.0f; // половина угла раствора внутреннего конуса
	float outer_degrees = 25.0f; // половина угла раствора внешнего конуса, не меньше inner
	Animation animation;

	// Положение и единичное направление оси с учётом анимации — уходят в шейдер и в визуализацию
	glm::vec3 worldPosition() const;
	glm::vec3 worldDirection() const;

	void drawUi();
};

// Все источники света сцены: хранит их параметры на CPU, рисует для них UI
// и загружает их в uniform-данные и storage-буферы сцены
class Lighting {
public:
	// Создаёт storage-буферы источников и привязывает их к набору сцены:
	// точечные — к binding 1, прожекторы — к binding 2
	bool create(VkDescriptorSet scene_set);
	void destroy();

	// Продвигает анимации всех источников на dt секунд
	void update(double dt);

	// Записывает рассеянный и направленный свет в uniform-данные сцены, загружает массивы точечных
	// источников и прожекторов в storage-буферы и записывает их число. Вызывать после ожидания fence
	// кадра и до привязки набора сцены (см. StorageArray::upload)
	void upload(shader::SceneUniforms& uniforms);

	// Секции всех источников света в текущем окне ImGui; точечные источники и прожекторы
	// можно добавлять и удалять
	void drawUi();

	// Текущие источники — для их визуализации
	const std::vector<PointLight>& pointLights() const;
	const std::vector<SpotLight>& spotLights() const;

private:
	AmbientLight ambient;
	DirectionalLight directional;

	// Стартовые точечные источники: тёплый свет рядом с кубами и розовый, кружащий вокруг них по орбите
	// выше верхних граней кубов (на их уровне свет падал бы на верх кубов вскользь)
	std::vector<PointLight> point_lights = {
		{ .position = glm::vec3(1.0f, 1.0f, 1.5f), .color = glm::vec3(1.0f, 0.85f, 0.6f), .intensity = 4.0f },
		{
			.position = glm::vec3(0.0f, 1.0f, 0.0f),
			.color = glm::vec3(1.0f, 0.3f, 0.7f),
			.intensity = 3.0f,
			.animation = Animation(true, 3.0f, 0.8f, glm::vec3(0.0f)),
		},
	};

	// Стартовые прожекторы: холодный, бьющий вниз на пол слева от кубов, и пара тёплый + холодный,
	// светящая с боков в центр куба у стены справа (см. Scene::createModels). Каждый красит свою боковую
	// грань, а не одни и те же: сумма тёплого и холодного дала бы белый. Интенсивность подобрана так,
	// чтобы на гранях свет не превышал 1 и цвет не «выгорал» в белый; внешний угол — чтобы край
	// конуса доставал до стены за кубом
	std::vector<SpotLight> spot_lights = {
		{ .position = glm::vec3(-2.5f, 2.0f, 0.0f), .color = glm::vec3(0.6f, 0.8f, 1.0f) },
		{
			.position = glm::vec3(1.5f, 1.0f, -2.0f),
			.direction = glm::vec3(2.5f, -1.5f, 0.0f),
			.color = glm::vec3(1.0f, 0.6f, 0.25f),
			.intensity = 6.0f,
			.inner_degrees = 15.0f,
			.outer_degrees = 30.0f,
		},
		{
			.position = glm::vec3(6.5f, 1.0f, -2.0f),
			.direction = glm::vec3(-2.5f, -1.5f, 0.0f),
			.color = glm::vec3(0.35f, 0.6f, 1.0f),
			.intensity = 6.0f,
			.inner_degrees = 15.0f,
			.outer_degrees = 30.0f,
		},
	};

	StorageArray point_light_buffer;
	StorageArray spot_light_buffer;
};

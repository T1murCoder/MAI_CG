#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include "model.hpp"
#include "shader_types.hpp"

// Карты теней: карта направленного света, массив карт прожекторов и массив кубических карт точечных
// источников; общие для них сэмплер со сравнением глубины, render pass и pipeline прохода теней.
// Размер карт не зависит от окна, поэтому при ресайзе они не пересоздаются
class Shadows {
public:
	// Выбирает формат карт, создаёт сэмплер, render pass и карты всех источников
	bool create();

	// Создаёт пайплайны проходов теней под pipeline layout сцены (вызывать после create)
	bool createPipeline(VkPipelineLayout layout);

	// Уничтожает ресурсы в порядке, обратном созданию; GPU уже должен простаивать
	void destroy();

	// Записывает карты с сэмплером в набор сцены (combined image sampler): направленный свет — binding 3,
	// массив карт прожекторов — binding 4, массив кубических карт точечных источников — binding 5
	void bind(VkDescriptorSet scene_set) const;

	// Записывает настройки теней в uniform-данные сцены
	void upload(shader::SceneUniforms& uniforms) const;

	// Записывает проходы теней: рисует модели, отбрасывающие тени, с точки зрения направленного света
	// (нет матрицы — его карта только очищается), каждого прожектора с тенью (i-я матрица
	// spot_view_projections — слой i) и в 6 граней куба каждого точечного источника с тенью (i-е положение
	// point_positions — кубическая карта i). Вызывать вне render pass, до основного прохода;
	// uniform-буферы моделей уже должны быть обновлены
	void record(VkCommandBuffer command_buffer, const std::optional<glm::mat4>& directional_view_projection,
				std::span<const glm::mat4> spot_view_projections, std::span<const glm::vec3> point_positions,
				std::span<const Model> models) const;

	// Секция настроек теней в текущем окне ImGui
	void drawUi();

private:
	// Массив квадратных карт глубины одного размера: image из layer_count слоёв, view всего массива
	// для чтения шейдером и по view + framebuffer на каждый слой для рендера в него
	struct MapArray {
		uint32_t size = 0;
		uint32_t layer_count = 0;
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkImageView sampled_view = VK_NULL_HANDLE;
		std::vector<VkImageView> layer_views;
		std::vector<VkFramebuffer> framebuffers;
	};

	bool createSampler();
	bool createRenderPass();

	// Создаёт массив карт; sampled_view_type — тип view для шейдера (2D, 2D_ARRAY, CUBE_ARRAY),
	// image_flags — флаги image (CUBE_COMPATIBLE для кубических карт)
	bool createMapArray(uint32_t size, uint32_t layer_count, VkImageViewType sampled_view_type,
						VkImageCreateFlags image_flags, MapArray& out) const;
	void destroyMapArray(MapArray& maps) const;

	// Начинает render pass теней: очищает слой framebuffer размера size и задаёт viewport и scissor под него
	void beginPass(VkCommandBuffer command_buffer, VkFramebuffer framebuffer, uint32_t size) const;

	// Только очищает слой: в нём нет источника, но render pass переводит его в layout для чтения
	void clearPass(VkCommandBuffer command_buffer, VkFramebuffer framebuffer, uint32_t size) const;

	// Проход 2D-карты (направленный свет, прожектор): глубина моделей с матрицей light_view_projection
	void recordPass(VkCommandBuffer command_buffer, VkFramebuffer framebuffer, uint32_t size,
					const glm::mat4& light_view_projection, std::span<const Model> models) const;

	// Проход грани face кубической карты точечного источника в light_position: расстояние до источника
	void recordPointFacePass(VkCommandBuffer command_buffer, VkFramebuffer framebuffer, glm::vec3 light_position,
							 uint32_t face, std::span<const Model> models) const;

	// Рисует текущим pipeline модели, отбрасывающие тени; push constant уже переданы
	void drawModels(VkCommandBuffer command_buffer, std::span<const Model> models) const;

	// Стороны карт в текселях: направленный свет покрывает всю сцену, поэтому его карта крупнее
	static constexpr uint32_t kDirectionalMapSize = 2048;
	static constexpr uint32_t kSpotMapSize = 1024;
	static constexpr uint32_t kPointMapSize = 512; // сторона грани куба; граней 6, угол каждой 90°

	// Граней у кубической карты: слои точечного источника i — [6i, 6i + 6), порядок +X, −X, +Y, −Y, +Z, −Z
	static constexpr uint32_t kCubeFaces = 6;

	// Настройки выборки из карты (см. shadow_params в shader_types.hpp). Сдвиги заданы в текселях карты,
	// поэтому одинаково подходят и для направленного света, и для прожекторов, у которых тексель
	// растёт с расстоянием
	bool enabled = true;
	float depth_bias_texels = 1.5f;    // сдвиг точки к источнику перед сравнением с картой
	float normal_offset_texels = 2.0f; // сдвиг точки вдоль нормали (при скользящем свете)
	int pcf_radius = 1;                // ядро PCF (2r + 1)²: 0 — одна выборка, не больше 3 (слайдер в UI)

	// Режим отладки: вместо освещения выводится множитель тени одного источника (белое — свет доходит,
	// чёрное — тень). Значения debug_view — как shadow_debug.x в shader_types.hpp
	enum DebugView : int { kDebugOff, kDebugDirectional, kDebugSpot, kDebugPoint };
	int debug_view = kDebugOff;
	int debug_light = 0; // индекс прожектора или точечного источника в его массиве

	// Depth bias растеризатора при рисовании карты: постоянный (в единицах точности формата)
	// и пропорциональный наклону треугольника к источнику
	float raster_bias_constant = 1.25f;
	float raster_bias_slope = 1.75f;

	// Выбранный формат глубины и можно ли его фильтровать линейно (аппаратный 2×2 PCF)
	VkFormat format = VK_FORMAT_UNDEFINED;
	bool linear_filter = false;

	VkSampler sampler = VK_NULL_HANDLE;
	VkRenderPass render_pass = VK_NULL_HANDLE;

	MapArray directional_map; // один слой
	MapArray spot_maps;       // по слою на прожектор с тенью, Lighting::kMaxShadowedSpotLights слоёв
	MapArray point_maps;      // по 6 слоёв-граней на точечный источник с тенью

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE; // принадлежит сцене
	VkPipeline pipeline = VK_NULL_HANDLE;              // 2D-карты: только вершинный шейдер
	VkPipeline point_pipeline = VK_NULL_HANDLE;        // грани кубических карт: пишет расстояние до источника
};

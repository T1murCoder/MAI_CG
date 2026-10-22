#include "application.hpp"

#include <cstring>
#include <iostream>

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include "animation.hpp"
#include "mesh.hpp"
#include "pipeline.hpp"
#include "transform.hpp"
#include "vk_buffer.hpp"

namespace application {

namespace {

// Данные, передаваемые в вершинный шейдер через uniform-буфер
struct UBO {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

// Геометрия куба (общая для обоих объектов) и два независимых uniform-буфера - по одному на куб
vk_buffer::Buffer vertex_buffer;
vk_buffer::Buffer index_buffer;
vk_buffer::Buffer uniform_buffer;
vk_buffer::Buffer uniform_buffer_2;

// Режим проекции первого куба: true — перспективная, false — ортографическая
bool use_perspective_projection = true;

double last_update_time = -1.0; // -1 означает "ещё не было предыдущего кадра"

// Первый куб: трансформация из UI-слайдеров и анимация по орбите, включаемая из UI
Transform cube_transform;
Animation cube_animation;

// Цвет из UI (ColorEdit4); во фрагментном шейдере умножается на процедурный цвет вершин
glm::vec4 base_color = glm::vec4(1.0f);

// Один descriptor set layout на двоих и два независимых набора дескрипторов (по кубу на набор)
VkDescriptorSetLayout descriptor_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet descriptor_set;
VkDescriptorSet descriptor_set_2;

// Второй куб: стоит в точке (2, 0, 0) и постоянно вращается вокруг Y со скоростью 30°/с без орбиты;
// фиксированный цвет — никак не связан с UI-состоянием первого куба
Transform second_cube_transform = { .position = glm::vec3(2.0f, 0.0f, 0.0f) };
Animation second_cube_animation(true, 0.0f, 0.0f, glm::vec3(0.0f, 30.0f, 0.0f));
const glm::vec4 kSecondCubeColor = glm::vec4(1.0f, 0.55f, 0.15f, 1.0f);

// Pipeline и его layout общие для обоих кубов (различаются только дескрипторы и push constant)
VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// Создаёт вершинный, индексный и два uniform-буфера, сразу копируя в них данные куба
bool createBuffers() {
	// Вершинный и индексный буферы общие для обоих кубов — геометрия не дублируется
	const VkDeviceSize vertex_size = sizeof(mesh::cube_vertices[0]) * mesh::cube_vertices.size();
	if (!vk_buffer::create(vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer)) {
		return false;
	}
	std::memcpy(vertex_buffer.mapped, mesh::cube_vertices.data(), size_t(vertex_size));

	const VkDeviceSize index_size = sizeof(mesh::cube_indices[0]) * mesh::cube_indices.size();
	if (!vk_buffer::create(index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		return false;
	}
	std::memcpy(index_buffer.mapped, mesh::cube_indices.data(), size_t(index_size));

	// Два uniform-буфера под MVP — по одному на каждый из двух кубов сцены
	if (!vk_buffer::create(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer)) {
		return false;
	}

	if (!vk_buffer::create(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer_2)) {
		return false;
	}

	return true;
}

// Создаёт layout, пул и два набора дескрипторов — по одному на каждый uniform-буфер/куб
bool createDescriptors() {
	auto& context = graphics::internal::context;

	// Привязка 0: один uniform-буфер, используется в вершинном шейдере
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr,
									&descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
		return false;
	}

	// Пул рассчитан на 2 набора дескрипторов — по числу кубов на сцене
	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 4,
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 4,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	// Выделяем оба набора дескрипторов одним вызовом из одного layout
	const VkDescriptorSetLayout set_layouts[2] = { descriptor_set_layout, descriptor_set_layout };
	VkDescriptorSet sets[2] = {};

	const VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 2,
		.pSetLayouts = set_layouts,
	};

	if (vkAllocateDescriptorSets(context.device, &alloc_info, sets) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor sets\n";
		return false;
	}

	descriptor_set = sets[0];
	descriptor_set_2 = sets[1];

	// Привязываем каждый набор дескрипторов к своему uniform-буферу
	const VkDescriptorBufferInfo buffer_infos[2] = {
		{ .buffer = uniform_buffer.buffer, .offset = 0, .range = sizeof(UBO) },
		{ .buffer = uniform_buffer_2.buffer, .offset = 0, .range = sizeof(UBO) },
	};

	const VkWriteDescriptorSet writes[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_infos[0],
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set_2,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_infos[1],
		},
	};

	vkUpdateDescriptorSets(context.device, 2, writes, 0, nullptr);

	return true;
}

// Создаёт pipeline layout (набор дескрипторов + push constant с цветом) и сам graphics pipeline
bool createPipeline() {
	// Push constant с цветом из UI (vec4), доступен фрагментному шейдеру
	const VkPushConstantRange push_constant_range = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(glm::vec4),
	};

	if (!pipeline::createLayout({ &descriptor_set_layout, 1 }, { &push_constant_range, 1 },
								pipeline_layout)) {
		return false;
	}

	return pipeline::create({
		.vertex_shader = "cube.vert.spv",
		.fragment_shader = "cube.frag.spv",
		.layout = pipeline_layout,
	}, pipeline);
}

} // namespace

// Инициализация ресурсов сцены: буферы геометрии/uniform, дескрипторы, pipeline
bool initialize() {
	if (!createBuffers()) {
		return false;
	}

	if (!createDescriptors()) {
		return false;
	}

	if (!createPipeline()) {
		return false;
	}

	return true;
}

// Освобождает все Vulkan-ресурсы приложения перед завершением работы
void shutdown() {
	auto& context = graphics::internal::context;
	// Дожидаемся завершения всех операций на очереди, прежде чем уничтожать используемые ими ресурсы
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);

	vk_buffer::destroy(uniform_buffer);
	vk_buffer::destroy(uniform_buffer_2);
	vk_buffer::destroy(vertex_buffer);
	vk_buffer::destroy(index_buffer);
}

// Обновляет состояние анимации и рисует все элементы управления ImGui
void update(double time) {
	// Дельта времени между кадрами; на самом первом кадре считаем её нулевой
	const double dt = (last_update_time >= 0.0) ? (time - last_update_time) : 0.0;
	last_update_time = time;

	cube_animation.update(dt);
	second_cube_animation.update(dt);

	ImGui::Begin("Cube Controls");

	// Переключатель между перспективной и ортографической проекцией
	ImGui::SeparatorText("Projection");
	if (ImGui::RadioButton("Perspective", use_perspective_projection)) {
		use_perspective_projection = true;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", !use_perspective_projection)) {
		use_perspective_projection = false;
	}

	cube_transform.drawUi();
	cube_animation.drawUi();

	// Цвет, который во фрагментном шейдере умножается на процедурный цвет вершин
	ImGui::SeparatorText("Color");
	ImGui::ColorEdit4("Cube Color", &base_color.x);

	ImGui::End();

	ImGui::ShowDemoWindow();
}

// Записывает команды отрисовки кадра: считает матрицы и рисует оба куба в одном render pass
void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	// Соотношение сторон окна нужно живым каждый кадр — окно может быть изменено в размере
	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);

	UBO ubo{};
	ubo.model = cube_animation.apply(cube_transform).matrix();
	ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

	if (use_perspective_projection) {
		ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
	} else {
		// half_extent подобран так, чтобы куб был примерно того же размера, что и в перспективе
		constexpr float half_extent = 1.7f;
		ubo.proj = glm::ortho(-half_extent * aspect, half_extent * aspect, -half_extent, half_extent,
							  0.1f, 100.0f);
	}
	// В Vulkan clip space ось Y направлена вниз, в отличие от OpenGL, под который заточен GLM
	ubo.proj[1][1] *= -1.0f;

	// Копируем MVP первого куба напрямую в замапленную память его uniform-буфера
	std::memcpy(uniform_buffer.mapped, &ubo, sizeof(ubo));

	// Второй куб: своя трансформация и своя анимация, камера та же
	UBO ubo2{};
	ubo2.model = second_cube_animation.apply(second_cube_transform).matrix();
	ubo2.view = ubo.view;
	ubo2.proj = ubo.proj;

	std::memcpy(uniform_buffer_2.mapped, &ubo2, sizeof(ubo2));

	// Command buffer нужно начинать и завершать заново каждый кадр — фреймворк это не делает сам
	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	vkBeginCommandBuffer(fd.command_buffer, &begin_info);

	// Цвет очистки фона и значение очистки буфера глубины (1.0 — максимально далеко)
	const VkClearValue clear_values[] = {
		{ .color = { { 0.02f, 0.02f, 0.03f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Viewport и scissor заданы динамическими в pipeline, поэтому задаём их здесь на каждый кадр
	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = { .extent = context.swapchain_extent };
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	// Первый куб: свой набор дескрипторов, цвет из UI, общие вершинный/индексный буферы
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&descriptor_set, 0, nullptr);

	vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					   sizeof(glm::vec4), &base_color);

	const VkBuffer vertex_buffers[] = { vertex_buffer.buffer };
	const VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, vertex_buffers, offsets);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdDrawIndexed(fd.command_buffer, uint32_t(mesh::cube_indices.size()), 1, 0, 0, 0);

	// Второй куб: другой набор дескрипторов и фиксированный цвет; геометрия та же, буферы уже привязаны
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&descriptor_set_2, 0, nullptr);

	vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					   sizeof(glm::vec4), &kSecondCubeColor);

	vkCmdDrawIndexed(fd.command_buffer, uint32_t(mesh::cube_indices.size()), 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application

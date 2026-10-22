#include "application.hpp"

#include <vulkan/vulkan.h>

#include "scene.hpp"

namespace application {

namespace {

Scene scene;

double last_update_time = -1.0; // -1 означает "ещё не было предыдущего кадра"

} // namespace

bool initialize() {
	return scene.create();
}

// Освобождает все Vulkan-ресурсы приложения перед завершением работы
void shutdown() {
	// Дожидаемся завершения всех операций на очереди, прежде чем уничтожать используемые ими ресурсы
	vkQueueWaitIdle(graphics::internal::context.graphics_queue);

	scene.destroy();
}

// Продвигает анимации сцены и рисует её элементы управления ImGui
void update(double time) {
	// Дельта времени между кадрами; на самом первом кадре считаем её нулевой
	const double dt = (last_update_time >= 0.0) ? (time - last_update_time) : 0.0;
	last_update_time = time;

	scene.update(dt);
	scene.drawUi();
}

// Записывает команды отрисовки кадра: начинает render pass, задаёт viewport и отдаёт отрисовку сцене
void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

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

	// Viewport и scissor заданы динамическими во всех пайплайнах, поэтому задаём их здесь на каждый кадр
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

	// Соотношение сторон окна нужно живым каждый кадр — окно может быть изменено в размере
	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);
	scene.record(fd.command_buffer, aspect);

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application

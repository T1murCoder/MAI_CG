#include "material.hpp"

#include <cassert>
#include <iostream>

#include <imgui.h>

#include "graphics_internal.hpp"

namespace {

struct MaterialDefinition {
	const char* name;
	const char* albedo;
	const char* specular = nullptr;
	const char* emissive = nullptr;
};

// Порядок соответствует MaterialId. Необязательные карты подключаются здесь;
// их влияние на освещение добавляют фазы 5/6, независимо от выбора материала.
constexpr std::array kDefinitions = {
	MaterialDefinition{ "UV Grid", TEXTURE_DIR "uv_grid.png" },
	MaterialDefinition{ "Brick", TEXTURE_DIR "brick.png" },
	MaterialDefinition{ "Tiles", TEXTURE_DIR "tiles.png" },
};
static_assert(kDefinitions.size() == static_cast<size_t>(MaterialId::Count));

} // namespace

// Блеск меняется на порядки (1…512), поэтому у слайдера логарифмическая шкала
void MaterialParameters::drawUi() {
	ImGui::ColorEdit3("Albedo tint", &albedo.x);
	// Положительный минимум исключает вырождение UV и случайное отражение рисунка;
	// AlwaysClamp ограничивает также значения, введённые вручную через Ctrl+Click.
	ImGui::DragFloat("Repeat U", &uv_repeat.x, 0.05f, 0.1f, 32.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat("Repeat V", &uv_repeat.y, 0.05f, 0.1f, 32.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::ColorEdit3("Specular", &specular.x);
	ImGui::SliderFloat("Shininess", &shininess, 1.0f, 512.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
}

bool MaterialCatalog::create(const VkDescriptorBufferInfo& model_buffer) {
	if (!createTextures() || !createDescriptors(model_buffer)) {
		destroy();
		return false;
	}
	return true;
}

bool MaterialCatalog::createTextures() {
	if (!black_texture.create(TEXTURE_DIR "black.png", Texture::ColorSpace::Linear)) {
		return false;
	}
	for (size_t i = 0; i < resources.size(); ++i) {
		auto& resource = resources[i];
		const auto& definition = kDefinitions[i];
		if (!resource.albedo.create(definition.albedo) ||
			(definition.specular && !resource.specular.create(definition.specular, Texture::ColorSpace::Linear)) ||
			(definition.emissive && !resource.emissive.create(definition.emissive))) {
			return false;
		}
	}
	return true;
}

bool MaterialCatalog::createDescriptors(const VkDescriptorBufferInfo& model_buffer) {
	const auto device = graphics::internal::context.device;
	const VkDescriptorSetLayoutBinding bindings[] = {
		{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = uint32_t(std::size(bindings)),
		.pBindings = bindings,
	};
	if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create material descriptor set layout\n";
		return false;
	}
	const VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, uint32_t(kMaterialCount) },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, uint32_t(3 * kMaterialCount) },
	};
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = uint32_t(kMaterialCount),
		.poolSizeCount = uint32_t(std::size(pool_sizes)),
		.pPoolSizes = pool_sizes,
	};
	if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create material descriptor pool\n";
		return false;
	}
	for (size_t i = 0; i < resources.size(); ++i) {
		auto& resource = resources[i];
		const auto& definition = kDefinitions[i];
		const VkDescriptorSetAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &set_layout,
		};
		if (vkAllocateDescriptorSets(device, &alloc_info, &resource.descriptor_set) != VK_SUCCESS) {
			std::cerr << "Failed to allocate descriptor set for material '" << definition.name << "'\n";
			return false;
		}
		const VkDescriptorImageInfo images[] = {
			resource.albedo.descriptor(),
			(definition.specular ? resource.specular : black_texture).descriptor(),
			(definition.emissive ? resource.emissive : black_texture).descriptor(),
		};
		std::array<VkWriteDescriptorSet, 4> writes = {};
		writes[0] = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = resource.descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &model_buffer,
		};
		for (uint32_t map = 0; map < std::size(images); ++map) {
			writes[map + 1] = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = resource.descriptor_set,
				.dstBinding = map + 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &images[map],
			};
		}
		vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
	}
	return true;
}

void MaterialCatalog::destroy() {
	const auto device = graphics::internal::context.device;
	vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
	descriptor_pool = VK_NULL_HANDLE;
	set_layout = VK_NULL_HANDLE;
	for (auto& resource : resources) {
		resource.descriptor_set = VK_NULL_HANDLE;
		resource.emissive.destroy();
		resource.specular.destroy();
		resource.albedo.destroy();
	}
	black_texture.destroy();
}

VkDescriptorSet MaterialCatalog::descriptor(MaterialId id) const {
	const auto index = static_cast<size_t>(id);
	assert(index < resources.size());
	return resources[index].descriptor_set;
}

void MaterialCatalog::drawSelectionUi(MaterialId& selected) const {
	assert(static_cast<size_t>(selected) < kDefinitions.size());
	if (ImGui::BeginCombo("Texture material", kDefinitions[static_cast<size_t>(selected)].name)) {
		for (size_t i = 0; i < kDefinitions.size(); ++i) {
			const bool active = static_cast<size_t>(selected) == i;
			if (ImGui::Selectable(kDefinitions[i].name, active)) {
				selected = static_cast<MaterialId>(i);
			}
			if (active) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
}

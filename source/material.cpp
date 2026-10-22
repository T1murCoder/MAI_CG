#include "material.hpp"

#include <imgui.h>

// Блеск меняется на порядки (1…512), поэтому у слайдера логарифмическая шкала
void Material::drawUi() {
	ImGui::SeparatorText("Material");
	ImGui::ColorEdit3("Albedo", &albedo.x);
	ImGui::ColorEdit3("Specular", &specular.x);
	ImGui::SliderFloat("Shininess", &shininess, 1.0f, 512.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
}

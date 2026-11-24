#pragma once

#include "ui/staff_renderer.hpp"

#include <vector>

namespace selfano::ui {

enum class ShowcasePreset : int {
  Phase1Overview = 0,
  ScaleAndCadence = 1,
  LedgerExtremes = 2,
  SecondAndAccidentalStress = 3,
  ClassicalNotationSpecimen = 4,
};

class StaffShowcase {
public:
  StaffShowcase();

  void set_preset(ShowcasePreset preset);
  void draw(ImFont* smufl_font, ImFont* ui_font, const char* smufl_path_label);
  void draw_contents(ImFont* smufl_font, ImFont* ui_font);

private:
  void rebuild_measures();

  StaffRenderOptions options_{};
  ShowcasePreset preset_{ShowcasePreset::Phase1Overview};
  std::vector<StaffMeasure> measures_;
};

} // namespace selfano::ui

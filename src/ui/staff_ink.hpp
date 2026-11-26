#pragma once

#include "imgui.h"

#include <array>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace selfano::ui::detail {

// A group of notation marks in staff-local pixels. The footprint is split along
// curves so a marking under one end needn't clear the height of the entire arch.
struct InkBox {
  float left, top, right, bottom;
};

class StaffInk {
public:
  void text(ImFont* font, float size, ImVec2 pos, ImU32 color, const char* value);
  void line(ImVec2 a, ImVec2 b, ImU32 color, float thickness);
  void quad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color);
  void curve(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color, float thickness, int segments);

  // Include non-glyph obstacles such as the staff itself or a clickable label.
  void reserve(InkBox box);
  void append(StaffInk group, float dy = 0.0f);
  void paint(ImDrawList& draw, ImVec2 origin) const;

  [[nodiscard]] std::span<const InkBox> footprint() const { return boxes_; }
  [[nodiscard]] float top(float fallback) const;
  [[nodiscard]] float bottom(float fallback) const;
  // Signed vertical movement to keep this group on the chosen side of obstacles.
  [[nodiscard]] float clearance(const StaffInk& obstacles, bool above, float gap) const;

private:
  struct Text {
    ImFont* font;
    float size;
    ImVec2 pos;
    std::string value;
  };
  enum class Shape { Line, Quad, Curve };
  struct Path {
    Shape shape;
    std::array<ImVec2, 4> points;
    float thickness;
    int segments;
  };
  struct Mark {
    std::variant<Text, Path> geometry;
    ImU32 color;
    float dy{};
  };
  std::vector<Mark> marks_;
  std::vector<InkBox> boxes_;
};

} // namespace selfano::ui::detail

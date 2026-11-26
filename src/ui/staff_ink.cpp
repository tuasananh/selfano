#include "ui/staff_ink.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace selfano::ui::detail {
namespace {
InkBox segment_bounds(ImVec2 a, ImVec2 b, float pad) {
  return {std::min(a.x, b.x) - pad, std::min(a.y, b.y) - pad, std::max(a.x, b.x) + pad,
          std::max(a.y, b.y) + pad};
}
} // namespace

void StaffInk::reserve(InkBox box) {
  boxes_.push_back(box);
}

void StaffInk::text(ImFont* font, float size, ImVec2 pos, ImU32 color, const char* value) {
  if (!font || !value || !*value) {
    return;
  }
  auto* baked = font->GetFontBaked(size);
  const float scale = size / baked->Size;
  pos = {std::round(pos.x), std::round(pos.y)};
  float x = pos.x;
  float y = pos.y;
  for (const char* cursor = value; *cursor;) {
    unsigned int cp{};
    cursor += ImTextCharFromUtf8(&cp, cursor, nullptr);
    if (cp == '\n') {
      x = pos.x;
      y += size;
      continue;
    }
    if (cp == '\r') {
      continue;
    }
    const auto* glyph = baked->FindGlyph(static_cast<ImWchar>(cp));
    if (!glyph) {
      continue;
    }
    if (glyph->Visible) {
      reserve({x + glyph->X0 * scale, y + glyph->Y0 * scale, x + glyph->X1 * scale,
               y + glyph->Y1 * scale});
    }
    x += glyph->AdvanceX * scale;
  }
  marks_.push_back({Text{font, size, pos, value}, color});
}

void StaffInk::line(ImVec2 a, ImVec2 b, ImU32 color, float thickness) {
  reserve(segment_bounds(a, b, thickness * 0.5f));
  marks_.push_back({Path{Shape::Line, {a, b, {}, {}}, thickness, 0}, color});
}
void StaffInk::quad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color) {
  reserve({std::min({a.x, b.x, c.x, d.x}), std::min({a.y, b.y, c.y, d.y}),
           std::max({a.x, b.x, c.x, d.x}), std::max({a.y, b.y, c.y, d.y})});
  marks_.push_back({Path{Shape::Quad, {a, b, c, d}, 0, 0}, color});
}
void StaffInk::curve(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color, float thickness,
                     int segments) {
  ImVec2 prev = a;
  for (int i = 1; i <= segments; ++i) {
    const auto next =
        ImBezierCubicCalc(a, b, c, d, static_cast<float>(i) / static_cast<float>(segments));
    reserve(segment_bounds(prev, next, thickness * 0.5f));
    prev = next;
  }
  marks_.push_back({Path{Shape::Curve, {a, b, c, d}, thickness, segments}, color});
}

void StaffInk::append(StaffInk group, float dy) {
  for (auto& mark : group.marks_) {
    mark.dy += dy;
    marks_.push_back(std::move(mark));
  }
  for (auto box : group.boxes_) {
    box.top += dy;
    box.bottom += dy;
    reserve(box);
  }
}

float StaffInk::top(float fallback) const {
  for (const auto& box : boxes_) {
    fallback = std::min(fallback, box.top);
  }
  return fallback;
}
float StaffInk::bottom(float fallback) const {
  for (const auto& box : boxes_) {
    fallback = std::max(fallback, box.bottom);
  }
  return fallback;
}
float StaffInk::clearance(const StaffInk& obstacles, bool above, float gap) const {
  float dy = 0;
  for (const auto& box : boxes_) {
    for (const auto& other : obstacles.boxes_) {
      if (box.right + 1 < other.left || box.left > other.right + 1) {
        continue;
      }
      dy = above ? std::min(dy, other.top - gap - box.bottom)
                 : std::max(dy, other.bottom + gap - box.top);
    }
  }
  return above ? std::floor(dy) : std::ceil(dy);
}

void StaffInk::paint(ImDrawList& draw, ImVec2 origin) const {
  for (const auto& mark : marks_) {
    auto translated = [&](ImVec2 p) { return ImVec2(origin.x + p.x, origin.y + mark.dy + p.y); };
    if (const auto* txt = std::get_if<Text>(&mark.geometry)) {
      draw.AddText(txt->font, txt->size, translated(txt->pos), mark.color, txt->value.c_str());
      continue;
    }
    const auto& path = std::get<Path>(mark.geometry);
    const auto& p = path.points;
    switch (path.shape) {
    case Shape::Line:
      draw.AddLine(translated(p[0]), translated(p[1]), mark.color, path.thickness);
      break;
    case Shape::Quad:
      draw.AddQuadFilled(translated(p[0]), translated(p[1]), translated(p[2]), translated(p[3]),
                         mark.color);
      break;
    case Shape::Curve:
      draw.AddBezierCubic(translated(p[0]), translated(p[1]), translated(p[2]), translated(p[3]),
                          mark.color, path.thickness, path.segments);
      break;
    }
  }
}

} // namespace selfano::ui::detail

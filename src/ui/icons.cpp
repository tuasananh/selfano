#include "ui/icons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace selfano::ui {

void draw_icon(ImDrawList* dl, IconId icon, ImVec2 c, float size, ImU32 col, float stroke) {
  if (dl == nullptr || icon == IconId::None || size <= 1.0f) {
    return;
  }
  const float r = size * 0.5f;
  const float s = std::max(1.2f, stroke * (size / 16.0f));

  switch (icon) {
  case IconId::None:
    break;

  case IconId::Home: {
    // Minimal roof + house outline
    const ImVec2 roof_l(c.x - r * 0.78f, c.y - r * 0.05f);
    const ImVec2 roof_t(c.x, c.y - r * 0.76f);
    const ImVec2 roof_r(c.x + r * 0.78f, c.y - r * 0.05f);
    dl->AddLine(roof_l, roof_t, col, s);
    dl->AddLine(roof_t, roof_r, col, s);
    const ImVec2 body_l(c.x - r * 0.56f, c.y - r * 0.14f);
    const ImVec2 body_bl(c.x - r * 0.56f, c.y + r * 0.72f);
    const ImVec2 body_br(c.x + r * 0.56f, c.y + r * 0.72f);
    const ImVec2 body_r(c.x + r * 0.56f, c.y - r * 0.14f);
    dl->AddLine(body_l, body_bl, col, s);
    dl->AddLine(body_bl, body_br, col, s);
    dl->AddLine(body_br, body_r, col, s);
    break;
  }

  case IconId::PlayCircle: {
    dl->AddCircle(c, r * 0.82f, col, 24, s);
    const ImVec2 p0(c.x - r * 0.24f, c.y - r * 0.36f);
    const ImVec2 p1(c.x - r * 0.24f, c.y + r * 0.36f);
    const ImVec2 p2(c.x + r * 0.42f, c.y);
    dl->AddTriangleFilled(p0, p1, p2, col);
    break;
  }

  case IconId::Scale: {
    // Ascending 3-step bars (scale / warm-up)
    const float w = r * 0.32f;
    dl->AddRectFilled(ImVec2(c.x - r * 0.75f, c.y + r * 0.15f),
                      ImVec2(c.x - r * 0.75f + w, c.y + r * 0.72f), col, 2.0f);
    dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - r * 0.22f),
                      ImVec2(c.x + w * 0.5f, c.y + r * 0.72f), col, 2.0f);
    dl->AddRectFilled(ImVec2(c.x + r * 0.75f - w, c.y - r * 0.68f),
                      ImVec2(c.x + r * 0.75f, c.y + r * 0.72f), col, 2.0f);
    break;
  }

  case IconId::Bolt: {
    const std::array<ImVec2, 6> pts{{
        ImVec2(c.x + r * 0.12f, c.y - r * 0.85f),
        ImVec2(c.x - r * 0.56f, c.y + r * 0.06f),
        ImVec2(c.x - r * 0.02f, c.y + r * 0.06f),
        ImVec2(c.x - r * 0.16f, c.y + r * 0.85f),
        ImVec2(c.x + r * 0.56f, c.y - r * 0.10f),
        ImVec2(c.x + r * 0.04f, c.y - r * 0.10f),
    }};
    dl->AddConvexPolyFilled(pts.data(), 3, col);
    const std::array<ImVec2, 3> tri2{pts[2], pts[3], pts[4]};
    const std::array<ImVec2, 3> tri3{pts[0], pts[2], pts[5]};
    dl->AddConvexPolyFilled(tri2.data(), 3, col);
    dl->AddConvexPolyFilled(tri3.data(), 3, col);
    break;
  }

  case IconId::MusicNote: {
    // Beamed eighth notes
    const float stem_x1 = c.x - r * 0.25f;
    const float stem_x2 = c.x + r * 0.52f;
    dl->AddLine(ImVec2(stem_x1, c.y + r * 0.42f), ImVec2(stem_x1, c.y - r * 0.55f), col, s);
    dl->AddLine(ImVec2(stem_x2, c.y + r * 0.28f), ImVec2(stem_x2, c.y - r * 0.68f), col, s);
    dl->AddLine(ImVec2(stem_x1, c.y - r * 0.55f), ImVec2(stem_x2, c.y - r * 0.68f), col, s * 1.5f);
    dl->AddCircleFilled(ImVec2(stem_x1 - r * 0.20f, c.y + r * 0.48f), r * 0.26f, col, 14);
    dl->AddCircleFilled(ImVec2(stem_x2 - r * 0.20f, c.y + r * 0.34f), r * 0.26f, col, 14);
    break;
  }

  case IconId::Chords: {
    // 3 stacked horizontal progression tiles
    dl->AddRect(ImVec2(c.x - r * 0.80f, c.y - r * 0.56f), ImVec2(c.x - r * 0.18f, c.y + r * 0.56f),
                col, 2.5f, 0, s);
    dl->AddRectFilled(ImVec2(c.x + r * 0.04f, c.y - r * 0.56f),
                      ImVec2(c.x + r * 0.80f, c.y + r * 0.56f), col, 2.5f);
    break;
  }

  case IconId::Layers: {
    // Stacked SRS cards
    dl->AddRect(ImVec2(c.x - r * 0.64f, c.y - r * 0.66f), ImVec2(c.x + r * 0.48f, c.y + r * 0.22f),
                col, 2.5f, 0, s);
    dl->AddLine(ImVec2(c.x - r * 0.40f, c.y + r * 0.48f), ImVec2(c.x + r * 0.66f, c.y + r * 0.48f),
                col, s);
    dl->AddLine(ImVec2(c.x + r * 0.66f, c.y + r * 0.48f), ImVec2(c.x + r * 0.66f, c.y - r * 0.36f),
                col, s);
    break;
  }

  case IconId::Headphones: {
    dl->PathClear();
    dl->PathArcTo(ImVec2(c.x, c.y + r * 0.05f), r * 0.68f, std::numbers::pi_v<float>, 6.28318f, 18);
    dl->PathStroke(col, 0, s);
    dl->AddRectFilled(ImVec2(c.x - r * 0.82f, c.y + r * 0.02f),
                      ImVec2(c.x - r * 0.42f, c.y + r * 0.68f), col, 2.5f);
    dl->AddRectFilled(ImVec2(c.x + r * 0.42f, c.y + r * 0.02f),
                      ImVec2(c.x + r * 0.82f, c.y + r * 0.68f), col, 2.5f);
    break;
  }

  case IconId::Wrench: {
    dl->AddLine(ImVec2(c.x - r * 0.60f, c.y + r * 0.60f), ImVec2(c.x + r * 0.30f, c.y - r * 0.30f),
                col, s * 1.2f);
    dl->AddCircle(ImVec2(c.x + r * 0.42f, c.y - r * 0.42f), r * 0.30f, col, 14, s);
    break;
  }

  case IconId::SidebarToggle: {
    dl->AddRect(ImVec2(c.x - r * 0.82f, c.y - r * 0.64f), ImVec2(c.x + r * 0.82f, c.y + r * 0.64f),
                col, 3.0f, 0, s);
    const float div_x = c.x - r * 0.24f;
    dl->AddLine(ImVec2(div_x, c.y - r * 0.64f), ImVec2(div_x, c.y + r * 0.64f), col, s);
    dl->AddLine(ImVec2(c.x - r * 0.62f, c.y - r * 0.24f), ImVec2(c.x - r * 0.42f, c.y - r * 0.24f),
                col, s);
    dl->AddLine(ImVec2(c.x - r * 0.62f, c.y + r * 0.08f), ImVec2(c.x - r * 0.42f, c.y + r * 0.08f),
                col, s);
    break;
  }

  case IconId::Piano: {
    dl->AddRect(ImVec2(c.x - r * 0.84f, c.y - r * 0.58f), ImVec2(c.x + r * 0.84f, c.y + r * 0.58f),
                col, 2.5f, 0, s);
    dl->AddLine(ImVec2(c.x - r * 0.28f, c.y - r * 0.58f), ImVec2(c.x - r * 0.28f, c.y + r * 0.58f),
                col, s * 0.75f);
    dl->AddLine(ImVec2(c.x + r * 0.28f, c.y - r * 0.58f), ImVec2(c.x + r * 0.28f, c.y + r * 0.58f),
                col, s * 0.75f);
    dl->AddRectFilled(ImVec2(c.x - r * 0.44f, c.y - r * 0.58f),
                      ImVec2(c.x - r * 0.12f, c.y + r * 0.08f), col, 1.0f);
    dl->AddRectFilled(ImVec2(c.x + r * 0.12f, c.y - r * 0.58f),
                      ImVec2(c.x + r * 0.44f, c.y + r * 0.08f), col, 1.0f);
    break;
  }

  case IconId::MidiPlug: {
    // 5-pin DIN MIDI connector icon
    dl->AddCircle(c, r * 0.78f, col, 20, s);
    const float pr = std::max(1.2f, r * 0.12f);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.42f, c.y + r * 0.08f), pr, col, 8);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.24f, c.y - r * 0.30f), pr, col, 8);
    dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), pr, col, 8);
    dl->AddCircleFilled(ImVec2(c.x + r * 0.24f, c.y - r * 0.30f), pr, col, 8);
    dl->AddCircleFilled(ImVec2(c.x + r * 0.42f, c.y + r * 0.08f), pr, col, 8);
    break;
  }

  case IconId::Sliders: {
    const float y0 = c.y - r * 0.45f;
    const float y1 = c.y;
    const float y2 = c.y + r * 0.45f;
    dl->AddLine(ImVec2(c.x - r * 0.76f, y0), ImVec2(c.x + r * 0.76f, y0), col, s);
    dl->AddLine(ImVec2(c.x - r * 0.76f, y1), ImVec2(c.x + r * 0.76f, y1), col, s);
    dl->AddLine(ImVec2(c.x - r * 0.76f, y2), ImVec2(c.x + r * 0.76f, y2), col, s);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.25f, y0), r * 0.20f, col, 12);
    dl->AddCircleFilled(ImVec2(c.x + r * 0.30f, y1), r * 0.20f, col, 12);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.10f, y2), r * 0.20f, col, 12);
    break;
  }

  case IconId::Sun: {
    dl->AddCircle(c, r * 0.36f, col, 16, s);
    for (int i = 0; i < 8; ++i) {
      const float a = static_cast<float>(i) * 0.785398f;
      const float ca = std::cos(a);
      const float sa = std::sin(a);
      dl->AddLine(ImVec2(c.x + ca * r * 0.56f, c.y + sa * r * 0.56f),
                  ImVec2(c.x + ca * r * 0.84f, c.y + sa * r * 0.84f), col, s);
    }
    break;
  }

  case IconId::Moon: {
    dl->PathClear();
    dl->PathArcTo(c, r * 0.70f, 0.55f, 5.25f, 20);
    dl->PathStroke(col, 0, s * 1.15f);
    dl->PathClear();
    dl->PathArcTo(ImVec2(c.x + r * 0.28f, c.y - r * 0.22f), r * 0.52f, 1.3f, 4.3f, 16);
    dl->PathStroke(col, 0, s);
    break;
  }

  case IconId::Play: {
    const ImVec2 p0(c.x - r * 0.42f, c.y - r * 0.62f);
    const ImVec2 p1(c.x - r * 0.42f, c.y + r * 0.62f);
    const ImVec2 p2(c.x + r * 0.64f, c.y);
    dl->AddTriangleFilled(p0, p1, p2, col);
    break;
  }

  case IconId::Pause: {
    dl->AddRectFilled(ImVec2(c.x - r * 0.52f, c.y - r * 0.60f),
                      ImVec2(c.x - r * 0.14f, c.y + r * 0.60f), col, 2.0f);
    dl->AddRectFilled(ImVec2(c.x + r * 0.14f, c.y - r * 0.60f),
                      ImVec2(c.x + r * 0.52f, c.y + r * 0.60f), col, 2.0f);
    break;
  }

  case IconId::Stop: {
    dl->AddRectFilled(ImVec2(c.x - r * 0.52f, c.y - r * 0.52f),
                      ImVec2(c.x + r * 0.52f, c.y + r * 0.52f), col, 2.5f);
    break;
  }

  case IconId::Refresh:
  case IconId::Repeat: {
    dl->PathClear();
    dl->PathArcTo(c, r * 0.66f, 0.45f, 5.55f, 20);
    dl->PathStroke(col, 0, s);
    const ImVec2 tip(c.x + r * 0.58f, c.y - r * 0.42f);
    dl->AddLine(tip, ImVec2(tip.x - r * 0.34f, tip.y - r * 0.04f), col, s);
    dl->AddLine(tip, ImVec2(tip.x - r * 0.02f, tip.y - r * 0.36f), col, s);
    break;
  }

  case IconId::VolumeOn: {
    const std::array<ImVec2, 4> cone{{
        ImVec2(c.x - r * 0.72f, c.y - r * 0.26f),
        ImVec2(c.x - r * 0.34f, c.y - r * 0.26f),
        ImVec2(c.x + r * 0.08f, c.y - r * 0.64f),
        ImVec2(c.x + r * 0.08f, c.y + r * 0.64f),
    }};
    dl->AddRectFilled(ImVec2(c.x - r * 0.72f, c.y - r * 0.26f),
                      ImVec2(c.x - r * 0.34f, c.y + r * 0.26f), col, 1.0f);
    dl->AddTriangleFilled(cone[1], cone[2], cone[3], col);
    dl->AddTriangleFilled(cone[1], ImVec2(c.x - r * 0.34f, c.y + r * 0.26f), cone[3], col);
    dl->PathClear();
    dl->PathArcTo(ImVec2(c.x + r * 0.14f, c.y), r * 0.40f, -0.85f, 0.85f, 10);
    dl->PathStroke(col, 0, s);
    dl->PathClear();
    dl->PathArcTo(ImVec2(c.x + r * 0.14f, c.y), r * 0.68f, -0.85f, 0.85f, 12);
    dl->PathStroke(col, 0, s);
    break;
  }

  case IconId::VolumeOff: {
    dl->AddRectFilled(ImVec2(c.x - r * 0.72f, c.y - r * 0.26f),
                      ImVec2(c.x - r * 0.34f, c.y + r * 0.26f), col, 1.0f);
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.34f, c.y - r * 0.26f),
                          ImVec2(c.x + r * 0.06f, c.y - r * 0.62f),
                          ImVec2(c.x + r * 0.06f, c.y + r * 0.62f), col);
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.34f, c.y - r * 0.26f),
                          ImVec2(c.x - r * 0.34f, c.y + r * 0.26f),
                          ImVec2(c.x + r * 0.06f, c.y + r * 0.62f), col);
    dl->AddLine(ImVec2(c.x + r * 0.28f, c.y - r * 0.28f), ImVec2(c.x + r * 0.78f, c.y + r * 0.28f),
                col, s);
    dl->AddLine(ImVec2(c.x + r * 0.78f, c.y - r * 0.28f), ImVec2(c.x + r * 0.28f, c.y + r * 0.28f),
                col, s);
    break;
  }

  case IconId::Check: {
    dl->AddLine(ImVec2(c.x - r * 0.56f, c.y + r * 0.04f), ImVec2(c.x - r * 0.14f, c.y + r * 0.48f),
                col, s * 1.25f);
    dl->AddLine(ImVec2(c.x - r * 0.14f, c.y + r * 0.48f), ImVec2(c.x + r * 0.62f, c.y - r * 0.46f),
                col, s * 1.25f);
    break;
  }

  case IconId::ChevronLeft: {
    dl->AddLine(ImVec2(c.x + r * 0.24f, c.y - r * 0.52f), ImVec2(c.x - r * 0.32f, c.y), col, s);
    dl->AddLine(ImVec2(c.x - r * 0.32f, c.y), ImVec2(c.x + r * 0.24f, c.y + r * 0.52f), col, s);
    break;
  }

  case IconId::ChevronRight: {
    dl->AddLine(ImVec2(c.x - r * 0.24f, c.y - r * 0.52f), ImVec2(c.x + r * 0.32f, c.y), col, s);
    dl->AddLine(ImVec2(c.x + r * 0.32f, c.y), ImVec2(c.x - r * 0.24f, c.y + r * 0.52f), col, s);
    break;
  }

  case IconId::ChevronDown: {
    dl->AddLine(ImVec2(c.x - r * 0.50f, c.y - r * 0.22f), ImVec2(c.x, c.y + r * 0.30f), col, s);
    dl->AddLine(ImVec2(c.x, c.y + r * 0.30f), ImVec2(c.x + r * 0.50f, c.y - r * 0.22f), col, s);
    break;
  }

  case IconId::Plus: {
    dl->AddLine(ImVec2(c.x - r * 0.62f, c.y), ImVec2(c.x + r * 0.62f, c.y), col, s * 1.15f);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.62f), ImVec2(c.x, c.y + r * 0.62f), col, s * 1.15f);
    break;
  }

  case IconId::Trash: {
    dl->AddLine(ImVec2(c.x - r * 0.66f, c.y - r * 0.44f), ImVec2(c.x + r * 0.66f, c.y - r * 0.44f),
                col, s);
    dl->AddLine(ImVec2(c.x - r * 0.24f, c.y - r * 0.68f), ImVec2(c.x + r * 0.24f, c.y - r * 0.68f),
                col, s);
    dl->AddRect(ImVec2(c.x - r * 0.48f, c.y - r * 0.44f), ImVec2(c.x + r * 0.48f, c.y + r * 0.70f),
                col, 2.0f, 0, s);
    dl->AddLine(ImVec2(c.x - r * 0.16f, c.y - r * 0.16f), ImVec2(c.x - r * 0.16f, c.y + r * 0.42f),
                col, s * 0.85f);
    dl->AddLine(ImVec2(c.x + r * 0.16f, c.y - r * 0.16f), ImVec2(c.x + r * 0.16f, c.y + r * 0.42f),
                col, s * 0.85f);
    break;
  }

  case IconId::Flame: {
    // Stylized droplet / flame silhouette
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.22f), r * 0.48f, col, 16);
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.46f, c.y + r * 0.14f),
                          ImVec2(c.x + r * 0.46f, c.y + r * 0.14f),
                          ImVec2(c.x + r * 0.04f, c.y - r * 0.82f), col);
    break;
  }

  case IconId::Sparkles: {
    // 4-point star
    dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.82f), ImVec2(c.x - r * 0.22f, c.y),
                          ImVec2(c.x + r * 0.22f, c.y), col);
    dl->AddTriangleFilled(ImVec2(c.x, c.y + r * 0.82f), ImVec2(c.x - r * 0.22f, c.y),
                          ImVec2(c.x + r * 0.22f, c.y), col);
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.82f, c.y), ImVec2(c.x, c.y - r * 0.22f),
                          ImVec2(c.x, c.y + r * 0.22f), col);
    dl->AddTriangleFilled(ImVec2(c.x + r * 0.82f, c.y), ImVec2(c.x, c.y - r * 0.22f),
                          ImVec2(c.x, c.y + r * 0.22f), col);
    break;
  }

  case IconId::Metronome: {
    const std::array<ImVec2, 4> body{{
        ImVec2(c.x - r * 0.34f, c.y - r * 0.72f),
        ImVec2(c.x + r * 0.34f, c.y - r * 0.72f),
        ImVec2(c.x + r * 0.66f, c.y + r * 0.72f),
        ImVec2(c.x - r * 0.66f, c.y + r * 0.72f),
    }};
    dl->AddPolyline(body.data(), 4, col, ImDrawFlags_Closed, s);
    dl->AddLine(ImVec2(c.x, c.y + r * 0.50f), ImVec2(c.x + r * 0.44f, c.y - r * 0.46f), col, s);
    break;
  }

  case IconId::AlertTriangle: {
    const std::array<ImVec2, 3> tri{{
        ImVec2(c.x, c.y - r * 0.74f),
        ImVec2(c.x + r * 0.78f, c.y + r * 0.66f),
        ImVec2(c.x - r * 0.78f, c.y + r * 0.66f),
    }};
    dl->AddPolyline(tri.data(), 3, col, ImDrawFlags_Closed, s);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.24f), ImVec2(c.x, c.y + r * 0.20f), col, s);
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.42f), std::max(1.2f, r * 0.10f), col, 8);
    break;
  }

  case IconId::SkipForward: {
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.62f, c.y - r * 0.54f),
                          ImVec2(c.x - r * 0.62f, c.y + r * 0.54f), ImVec2(c.x + r * 0.22f, c.y),
                          col);
    dl->AddRectFilled(ImVec2(c.x + r * 0.34f, c.y - r * 0.54f),
                      ImVec2(c.x + r * 0.62f, c.y + r * 0.54f), col, 1.5f);
    break;
  }

  case IconId::DotsHorizontal: {
    const float dr = std::max(1.6f, r * 0.17f);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.55f, c.y), dr, col, 10);
    dl->AddCircleFilled(ImVec2(c.x, c.y), dr, col, 10);
    dl->AddCircleFilled(ImVec2(c.x + r * 0.55f, c.y), dr, col, 10);
    break;
  }

  case IconId::Edit: {
    dl->AddLine(ImVec2(c.x - r * 0.58f, c.y + r * 0.58f), ImVec2(c.x + r * 0.54f, c.y - r * 0.54f),
                col, s * 1.3f);
    dl->AddLine(ImVec2(c.x - r * 0.68f, c.y + r * 0.68f), ImVec2(c.x - r * 0.30f, c.y + r * 0.56f),
                col, s);
    break;
  }

  case IconId::Eye: {
    dl->PathClear();
    dl->PathArcTo(ImVec2(c.x, c.y + r * 0.52f), r * 0.94f, 4.02f, 5.40f, 16);
    dl->PathStroke(col, 0, s);
    dl->PathClear();
    dl->PathArcTo(ImVec2(c.x, c.y - r * 0.52f), r * 0.94f, 0.88f, 2.26f, 16);
    dl->PathStroke(col, 0, s);
    dl->AddCircle(c, r * 0.24f, col, 14, s);
    break;
  }
  }
}

} // namespace selfano::ui

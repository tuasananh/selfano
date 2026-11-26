#pragma once

#include "core/lesson_score.hpp"
#include "core/musicxml_importer.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace selfano::ui {

class ImportLessonModal {
public:
  void open_preview(core::MusicXmlImportResult result);
  void open_edit(const core::LessonScore& score);

  void set_error(std::string message) { error_ = std::move(message); }
  void clear_error() noexcept { error_.clear(); }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }
  [[nodiscard]] std::string& mutable_error() noexcept { return error_; }
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] std::optional<core::LessonScore> draw();

private:
  void populate_draft(core::LessonScore score, bool is_editing, std::vector<std::string> warnings);

  bool open_{false};
  bool is_editing_{false};
  core::LessonScore draft_{};
  std::vector<std::string> warnings_{};
  std::string error_{};
  std::array<char, 160> title_buf_{};
  std::array<char, 160> composer_buf_{};
  float bpm_buf_{84.0f};
  std::vector<std::string> bar_chords_{};
  bool harmony_expanded_{false};
};

} // namespace selfano::ui

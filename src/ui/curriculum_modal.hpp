#pragma once

#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"
#include "core/lesson_score.hpp"
#include "core/profile_store.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::ui {

struct CurriculumModalActions {
  std::optional<std::string> select_lesson_id{};
  std::optional<core::LessonScore> edit_imported_lesson{};
  std::optional<std::string> delete_imported_lesson_id{};
  bool request_import_file_dialog{false};
  std::vector<core::LessonPlaybackNote> preview_playback_notes{};
};

class CurriculumModal {
public:
  void open() noexcept {
    open_ = true;
    active_guide_tier_.reset();
    scroll_to_active_frames_ = 2;
  }
  void open_for_lesson(bool is_imported) noexcept {
    open_ = true;
    tab_ = is_imported ? 1 : 0;
    active_guide_tier_.reset();
    scroll_to_active_frames_ = 2;
  }
  void open_chapter_guide(std::uint8_t tier) noexcept {
    open_ = true;
    tab_ = 0;
    active_guide_tier_ = tier;
  }
  void open_my_books() noexcept {
    open_ = true;
    tab_ = 1;
    active_guide_tier_.reset();
    scroll_to_active_frames_ = 2;
  }
  void close() noexcept {
    open_ = false;
    active_guide_tier_.reset();
  }
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] CurriculumModalActions draw(const core::LessonLibrary& library,
                                            const core::UserProfile& profile,
                                            std::string_view active_lesson_id,
                                            std::int64_t today_epoch_day, std::string& import_error,
                                            bool import_modal_open);

private:
  bool open_{false};
  int tab_{0};         // 0 = Tiers 1-6, 1 = My Books
  int tier_filter_{0}; // 0 = All, 1..6 = T1..T6
  int scroll_to_active_frames_{0};
  std::optional<std::uint8_t> active_guide_tier_{};
};

} // namespace selfano::ui

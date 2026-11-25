#pragma once

#include "core/lesson_library.hpp"
#include "core/lesson_score.hpp"
#include "core/profile_store.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace selfano::ui {

struct CurriculumModalActions {
  std::optional<std::string> select_lesson_id{};
  std::optional<core::LessonScore> edit_imported_lesson{};
  std::optional<std::string> delete_imported_lesson_id{};
  bool request_import_file_dialog{false};
};

class CurriculumModal {
public:
  void open() noexcept { open_ = true; }
  void open_my_books() noexcept {
    open_ = true;
    tab_ = 1;
  }
  void close() noexcept { open_ = false; }
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] CurriculumModalActions draw(const core::LessonLibrary& library,
                                            const core::UserProfile& profile,
                                            std::string_view active_lesson_id,
                                            std::int64_t today_epoch_day, std::string& import_error,
                                            bool import_modal_open);

private:
  bool open_{false};
  int tab_{0}; // 0 = Tiers 1-6, 1 = My Books
};

} // namespace selfano::ui

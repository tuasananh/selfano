#include "core/autopilot.hpp"
#include "core/lesson_score.hpp"
#include "core/musicxml_importer.hpp"

#include "app/app_controller.hpp"
#include "app/app_paths.hpp"
#include "app/cli_options.hpp"
#include "app/web_platform.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::app {

bool AppController::select_active_lesson(std::string_view lesson_id, double now_ms, bool persist) {
  const auto* score = lesson_library_.find_lesson(lesson_id);
  if (score == nullptr) {
    return false;
  }
  profile_.active_lesson_id = score->meta.id;
  const auto prog_it = profile_.lesson_progress.find(score->meta.id);
  lesson_view_.load_lesson(
      *score, prog_it == profile_.lesson_progress.end() ? core::LessonProgress{} : prog_it->second);
  lesson_view_.set_revision_context(false);
  sync_weekly_key_to_active_lesson(now_ms);
  if (persist) {
    persist_profile_now();
  }
  return true;
}

void AppController::save_and_activate_imported_lesson(const core::LessonScore& score,
                                                      double now_ms) {
  const auto saved = lesson_library_.save_imported_lesson(imported_lessons_dir_, score);
  if (!saved.has_value()) {
    lesson_view_.set_import_error(saved.error());
    return;
  }
  if (std::ranges::find(profile_.imported_lesson_ids, score.meta.id) ==
      profile_.imported_lesson_ids.end()) {
    profile_.imported_lesson_ids.push_back(score.meta.id);
  }
  (void)select_active_lesson(score.meta.id, now_ms, true);
  show_hud_toast("Saved to My Books: " + score.meta.title, now_ms);
}

void AppController::delete_imported_lesson(std::string_view lesson_id, double now_ms) {
  const auto del = lesson_library_.delete_imported_lesson_file(imported_lessons_dir_, lesson_id);
  if (!del.has_value()) {
    lesson_view_.set_import_error(del.error());
    return;
  }
  std::erase(profile_.imported_lesson_ids, lesson_id);
  profile_.lesson_progress.erase(std::string(lesson_id));
  if (profile_.active_lesson_id == lesson_id) {
    if (!select_active_lesson("t1_01", now_ms, false) &&
        !lesson_library_.built_in_lessons().empty()) {
      (void)select_active_lesson(lesson_library_.built_in_lessons().front()->meta.id, now_ms,
                                 false);
    }
  }
  persist_profile_now();
  show_hud_toast("Removed from My Books", now_ms);
}

void SDLCALL AppController::on_open_file_dialog(void* userdata, const char* const* filelist,
                                                int filter) {
  (void)filter;
  auto* ctx = static_cast<AppController*>(userdata);
  if (ctx == nullptr) {
    return;
  }
  std::scoped_lock lock(ctx->import_dialog_mutex_);
  if (filelist == nullptr) {
    ctx->pending_import_error_ = SDL_GetError();
    return;
  }
  if (*filelist != nullptr) {
    ctx->pending_import_paths_.emplace_back(*filelist);
  }
}

void AppController::request_musicxml_file_dialog() {
#ifdef __EMSCRIPTEN__
  web_open_file_picker();
#else
  static const SDL_DialogFileFilter kFilters[] = {
      {.name = "MusicXML Scores", .pattern = "musicxml;xml;mxl"},
      {.name = "All Files", .pattern = "*"},
  };
  SDL_ShowOpenFileDialog(on_open_file_dialog, this, window_, kFilters, 2, nullptr, false);
#endif
}

void AppController::handle_imported_file_path(const std::filesystem::path& path, double now_ms,
                                              bool auto_save_immediate) {
  const auto res = core::import_musicxml_file(path);
  if (!res.has_value()) {
    lesson_view_.set_import_error(res.error());
    app_mode_ = AppMode::FreePractice;
    free_stage_ = core::AutopilotStage::Stage3Lessons;
    return;
  }
  app_mode_ = AppMode::FreePractice;
  free_stage_ = core::AutopilotStage::Stage3Lessons;
  if (auto_save_immediate) {
    save_and_activate_imported_lesson(res->score, now_ms);
  } else {
    lesson_view_.open_import_preview(*res);
  }
}

void AppController::drain_pending_imports(double now_ms) {
  std::vector<std::string> paths;
  std::string dlg_err;
  {
    std::scoped_lock lock(import_dialog_mutex_);
    paths.swap(pending_import_paths_);
    dlg_err.swap(pending_import_error_);
  }
  if (!dlg_err.empty()) {
    lesson_view_.set_import_error("File dialog error: " + dlg_err);
  }
  for (const auto& p : paths) {
    handle_imported_file_path(std::filesystem::path(p), now_ms, false);
  }

  for (const auto& item : drain_web_imported_files()) {
    core::MusicXmlImportOptions opts{};
    const std::filesystem::path fp(item.filename);
    if (fp.has_stem()) {
      opts.fallback_title = fp.stem().string();
    }
    auto res = (core::is_mxl_zip_bytes(item.bytes) ||
                (item.filename.size() >= 4U && has_musicxml_extension(item.filename) &&
                 (item.filename.back() == 'l' || item.filename.back() == 'L')))
                   ? core::import_mxl_bytes(item.bytes, opts)
                   : core::import_musicxml_string(
                         std::string_view(reinterpret_cast<const char*>(item.bytes.data()),
                                          item.bytes.size()),
                         opts);
    if (!res.has_value()) {
      lesson_view_.set_import_error(res.error());
    } else {
      res->score.meta.source_filename = fp.filename().string();
      lesson_view_.open_import_preview(*res);
    }
    app_mode_ = AppMode::FreePractice;
    free_stage_ = core::AutopilotStage::Stage3Lessons;
  }
}

} // namespace selfano::app

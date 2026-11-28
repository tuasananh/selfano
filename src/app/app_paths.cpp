#include "app/app_paths.hpp"

#include <SDL3/SDL.h>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace selfano::app {

std::int64_t current_epoch_day() noexcept {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::floor<std::chrono::days>(now).time_since_epoch().count();
}

std::filesystem::path resolve_lessons_dir() {
  std::vector<std::filesystem::path> candidates;
  if (const char* base_path = SDL_GetBasePath(); base_path != nullptr) {
    candidates.emplace_back(std::filesystem::path(base_path) / "assets" / "lessons");
  }
  candidates.emplace_back(std::filesystem::path("assets") / "lessons");
  candidates.emplace_back(std::filesystem::path("/assets") / "lessons");
  std::error_code ec;
  for (const auto& c : candidates) {
    if (std::filesystem::exists(c / "curriculum.json", ec) && !ec) {
      return c;
    }
  }
  return std::filesystem::path("assets") / "lessons";
}

std::filesystem::path resolve_sight_reading_dir() {
  std::vector<std::filesystem::path> candidates;
  if (const char* base_path = SDL_GetBasePath(); base_path != nullptr) {
    candidates.emplace_back(std::filesystem::path(base_path) / "assets" / "sight_reading");
  }
  candidates.emplace_back(std::filesystem::path("assets") / "sight_reading");
  candidates.emplace_back(std::filesystem::path("/assets") / "sight_reading");
  std::error_code ec;
  for (const auto& c : candidates) {
    if (std::filesystem::exists(c / "corpus.json", ec) && !ec) {
      return c;
    }
  }
  return std::filesystem::path("assets") / "sight_reading";
}

std::filesystem::path resolve_profile_path(const std::string& cli_override) {
  if (!cli_override.empty()) {
    return {cli_override};
  }
#ifdef __EMSCRIPTEN__
  return std::filesystem::path("/persist/profile.json");
#else
  std::unique_ptr<char, decltype(&SDL_free)> pref_dir(SDL_GetPrefPath("selfano", "selfano"),
                                                      &SDL_free);
  if (pref_dir != nullptr) {
    return std::filesystem::path(pref_dir.get()) / "profile.json";
  }
  return {"profile.json"};
#endif
}

std::filesystem::path resolve_imported_lessons_dir(const std::filesystem::path& profile_path) {
#ifdef __EMSCRIPTEN__
  (void)profile_path;
  return std::filesystem::path("/persist/lessons");
#else
  if (profile_path.has_parent_path()) {
    return profile_path.parent_path() / "lessons";
  }
  return {"lessons"};
#endif
}

bool has_musicxml_extension(std::string_view filename) noexcept {
  auto ends_ci = [&](std::string_view ext) noexcept {
    if (filename.size() < ext.size()) {
      return false;
    }
    const auto tail = filename.substr(filename.size() - ext.size());
    for (std::size_t i = 0; i < ext.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(tail[i])) != ext[i]) {
        return false;
      }
    }
    return true;
  };
  return ends_ci(".musicxml") || ends_ci(".xml") || ends_ci(".mxl");
}

} // namespace selfano::app

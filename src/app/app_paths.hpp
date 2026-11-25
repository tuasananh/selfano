#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace selfano::app {

[[nodiscard]] std::int64_t current_epoch_day() noexcept;
[[nodiscard]] std::filesystem::path resolve_lessons_dir();
[[nodiscard]] std::filesystem::path resolve_profile_path(const std::string& cli_override);
[[nodiscard]] std::filesystem::path
resolve_imported_lessons_dir(const std::filesystem::path& profile_path);
[[nodiscard]] bool has_musicxml_extension(std::string_view filename) noexcept;

} // namespace selfano::app

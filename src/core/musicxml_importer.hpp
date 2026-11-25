#pragma once

#include "core/lesson_score.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

inline constexpr std::size_t kMaxMxlCompressedBytes = std::size_t{16} * 1024U * 1024U;
inline constexpr std::size_t kMaxXmlPayloadBytes = std::size_t{32} * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaxImportMeasures = 512U;

struct MusicXmlImportOptions {
  std::string fallback_title{"Imported Piece"};
  double default_bpm{84.0};
  std::size_t max_measures{kDefaultMaxImportMeasures};
};

struct MusicXmlImportResult {
  LessonScore score{};
  std::vector<std::string> warnings;
};

[[nodiscard]] bool is_mxl_zip_bytes(std::span<const std::uint8_t> bytes) noexcept;

[[nodiscard]] std::string generate_imported_lesson_id(std::string_view title,
                                                      std::size_t measure_count,
                                                      std::string_view content_fingerprint);

[[nodiscard]] std::expected<MusicXmlImportResult, std::string>
import_musicxml_string(std::string_view xml_text, MusicXmlImportOptions options = {});

[[nodiscard]] std::expected<MusicXmlImportResult, std::string>
import_mxl_bytes(std::span<const std::uint8_t> zip_bytes, MusicXmlImportOptions options = {});

[[nodiscard]] std::expected<MusicXmlImportResult, std::string>
import_musicxml_file(const std::filesystem::path& path, MusicXmlImportOptions options = {});

} // namespace selfano::core

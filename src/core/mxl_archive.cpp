#include "core/musicxml_importer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <miniz.h>
#include <optional>
#include <pugixml.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace selfano::core {

namespace {

struct ZipReaderGuard {
  mz_zip_archive zip{};
  bool active{false};

  ZipReaderGuard() = default;
  ZipReaderGuard(const ZipReaderGuard&) = delete;
  ZipReaderGuard& operator=(const ZipReaderGuard&) = delete;

  ~ZipReaderGuard() {
    if (active) {
      mz_zip_reader_end(&zip);
    }
  }
};

[[nodiscard]] bool iends_with(std::string_view text, std::string_view suffix) noexcept {
  if (text.size() < suffix.size()) {
    return false;
  }
  const auto tail = text.substr(text.size() - suffix.size());
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(tail[i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string trim_copy(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
    sv.remove_suffix(1);
  }
  return std::string(sv);
}

[[nodiscard]] std::expected<std::string, std::string>
extract_zip_entry(mz_zip_archive& zip, mz_uint file_index, std::size_t max_bytes) {
  mz_zip_archive_file_stat st{};
  if (mz_zip_reader_file_stat(&zip, file_index, &st) != MZ_TRUE) {
    return std::unexpected("Failed reading MXL archive entry metadata");
  }
  if (st.m_is_directory != 0) {
    return std::unexpected("MXL rootfile entry is a directory");
  }
  if (st.m_uncomp_size == 0U || st.m_uncomp_size > max_bytes) {
    return std::unexpected("MXL uncompressed XML payload is empty or exceeds 32 MiB safety cap");
  }
  std::string buffer(static_cast<std::size_t>(st.m_uncomp_size), '\0');
  if (mz_zip_reader_extract_to_mem(&zip, file_index, buffer.data(), buffer.size(), 0) != MZ_TRUE) {
    return std::unexpected("Failed decompressing MusicXML entry from .mxl archive");
  }
  return buffer;
}

} // namespace

bool is_mxl_zip_bytes(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 4U && bytes[0] == 0x50U && bytes[1] == 0x4BU && bytes[2] == 0x03U &&
         bytes[3] == 0x04U;
}

std::expected<MusicXmlImportResult, std::string>
import_mxl_bytes(std::span<const std::uint8_t> zip_bytes, MusicXmlImportOptions options) {
  if (zip_bytes.empty()) {
    return std::unexpected(".mxl archive is empty");
  }
  if (zip_bytes.size() > kMaxMxlCompressedBytes) {
    return std::unexpected(".mxl archive exceeds 16 MiB size cap");
  }

  ZipReaderGuard guard{};
  if (mz_zip_reader_init_mem(&guard.zip, zip_bytes.data(), zip_bytes.size(), 0) != MZ_TRUE) {
    return std::unexpected("Invalid or corrupt .mxl ZIP archive");
  }
  guard.active = true;

  std::optional<std::string> rootfile_path;
  const int container_idx =
      mz_zip_reader_locate_file(&guard.zip, "META-INF/container.xml", nullptr, 0);
  if (container_idx >= 0) {
    const auto container_xml =
        extract_zip_entry(guard.zip, static_cast<mz_uint>(container_idx), kMaxXmlPayloadBytes);
    if (container_xml.has_value()) {
      pugi::xml_document cdoc;
      if (cdoc.load_buffer(container_xml->data(), container_xml->size())) {
        const pugi::xml_node rf = cdoc.child("container").child("rootfiles").child("rootfile");
        if (rf) {
          const std::string fp = trim_copy(rf.attribute("full-path").as_string());
          if (!fp.empty()) {
            rootfile_path = fp;
          }
        }
      }
    }
  }

  std::optional<mz_uint> target_index;
  if (rootfile_path.has_value()) {
    const int idx = mz_zip_reader_locate_file(&guard.zip, rootfile_path->c_str(), nullptr, 0);
    if (idx >= 0) {
      target_index = static_cast<mz_uint>(idx);
    }
  }

  if (!target_index.has_value()) {
    const mz_uint count = mz_zip_reader_get_num_files(&guard.zip);
    for (mz_uint i = 0; i < count; ++i) {
      mz_zip_archive_file_stat st{};
      if (mz_zip_reader_file_stat(&guard.zip, i, &st) != MZ_TRUE || st.m_is_directory != 0) {
        continue;
      }
      const std::string_view fname = st.m_filename;
      if (fname.starts_with("META-INF/")) {
        continue;
      }
      if (iends_with(fname, ".musicxml") || iends_with(fname, ".xml")) {
        target_index = i;
        break;
      }
    }
  }

  if (!target_index.has_value()) {
    return std::unexpected("No MusicXML rootfile found inside .mxl archive");
  }

  const auto xml_payload = extract_zip_entry(guard.zip, *target_index, kMaxXmlPayloadBytes);
  if (!xml_payload.has_value()) {
    return std::unexpected(xml_payload.error());
  }

  return import_musicxml_string(*xml_payload, std::move(options));
}

} // namespace selfano::core

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace selfano::app {

struct WebImportedFile {
  std::string filename;
  std::vector<std::uint8_t> bytes;
};

[[nodiscard]] int browser_viewport_width();
[[nodiscard]] int browser_viewport_height();

void idbfs_mount_and_load();
[[nodiscard]] bool idbfs_load_done() noexcept;
void idbfs_sync_to_disk();

void web_open_file_picker();
void web_install_drop_zone();
[[nodiscard]] std::vector<WebImportedFile> drain_web_imported_files();

} // namespace selfano::app

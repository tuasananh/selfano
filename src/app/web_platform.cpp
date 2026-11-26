#include "app/web_platform.hpp"

#include <vector>

#ifdef __EMSCRIPTEN__

#include <atomic>
#include <cstdint>
#include <emscripten.h>
#include <mutex>
#include <utility>

namespace {

std::atomic<bool> g_idbfs_load_done{false};
std::atomic<bool> g_idbfs_ok{false};

std::mutex g_web_import_mutex;
std::vector<selfano::app::WebImportedFile> g_web_import_queue;
std::vector<std::vector<std::uint8_t>> g_web_staging_buffers;

// clang-format off
EM_JS(int, selfano_browser_viewport_width, (), {
  const w = typeof window !== "undefined" ? (window.innerWidth | 0) : 1360;
  return w > 640 ? w : 640;
});

EM_JS(int, selfano_browser_viewport_height, (), {
  const h = typeof window !== "undefined" ? (window.innerHeight | 0) : 860;
  return h > 480 ? h : 480;
});

EM_JS(void, selfano_idbfs_mount_and_load, (), {
  globalThis.__selfanoIdbfsMounted = false;
  globalThis.__selfanoSyncInFlight = false;
  globalThis.__selfanoSyncQueued = false;
  try {
    try {
      FS.mkdir("/persist");
    } catch (mkdirErr) {
    }
    FS.mount(IDBFS, {}, "/persist");
    globalThis.__selfanoIdbfsMounted = true;
    FS.syncfs(true, function(err) {
      if (err) {
        console.warn("Selfano IDBFS initial load warning:", err);
      }
      if (Module._selfano_on_idbfs_loaded) {
        Module._selfano_on_idbfs_loaded(err ? 0 : 1);
      }
    });
  } catch (e) {
    console.warn("Selfano IDBFS mount fallback:", e);
    if (Module._selfano_on_idbfs_loaded) {
      Module._selfano_on_idbfs_loaded(0);
    }
  }
});

EM_JS(void, selfano_idbfs_sync_to_disk, (), {
  if (!globalThis.__selfanoIdbfsMounted) {
    return;
  }
  if (globalThis.__selfanoSyncInFlight) {
    globalThis.__selfanoSyncQueued = true;
    return;
  }
  globalThis.__selfanoSyncInFlight = true;
  const runSync = function() {
    FS.syncfs(false, function(err) {
      if (err) {
        console.warn("Selfano IDBFS save warning:", err);
      }
      if (globalThis.__selfanoSyncQueued) {
        globalThis.__selfanoSyncQueued = false;
        runSync();
      } else {
        globalThis.__selfanoSyncInFlight = false;
      }
    });
  };
  runSync();
});

EM_JS(void, selfano_ensure_send_helper_js, (), {
  if (globalThis.__selfanoSendImportedFile) {
    return;
  }
  globalThis.__selfanoSendImportedFile = function(file) {
    if (!file) return;
    file.arrayBuffer().then(function(buf) {
      const bytes = new Uint8Array(buf);
      const nameLen = Module.lengthBytesUTF8(file.name) + 1;
      const namePtr = Module._selfano_alloc_bytes(nameLen);
      const dataPtr = Module._selfano_alloc_bytes(bytes.length);
      if (namePtr && dataPtr) {
        Module.stringToUTF8(file.name, namePtr, nameLen);
        Module.HEAPU8.set(bytes, dataPtr);
        Module._selfano_on_imported_file(namePtr, dataPtr, bytes.length);
      }
      if (namePtr) Module._selfano_free_bytes(namePtr);
      if (dataPtr) Module._selfano_free_bytes(dataPtr);
    });
  };
});

EM_JS(void, selfano_web_open_file_picker, (), {
  if (typeof document === "undefined") {
    return;
  }
  let input = document.getElementById("selfano-musicxml-picker");
  if (!input) {
    input = document.createElement("input");
    input.id = "selfano-musicxml-picker";
    input.type = "file";
    input.accept = ".musicxml,.xml,.mxl";
    input.style.display = "none";
    document.body.appendChild(input);
    input.addEventListener("change", function(evt) {
      const files = evt.target && evt.target.files;
      if (!files || files.length === 0) return;
      if (globalThis.__selfanoSendImportedFile) {
        globalThis.__selfanoSendImportedFile(files[0]);
      }
      input.value = "";
    });
  }
  input.click();
});

EM_JS(void, selfano_web_install_drop_zone, (), {
  if (typeof document === "undefined" || globalThis.__selfanoDropInstalled) {
    return;
  }
  globalThis.__selfanoDropInstalled = true;
  const target = document.getElementById("canvas") || document.body;
  target.addEventListener("dragover", function(e) {
    e.preventDefault();
  });
  target.addEventListener("drop", function(e) {
    e.preventDefault();
    const files = e.dataTransfer && e.dataTransfer.files;
    if (!files || files.length === 0) return;
    if (globalThis.__selfanoSendImportedFile) {
      globalThis.__selfanoSendImportedFile(files[0]);
    }
  });
});
// clang-format on

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void selfano_on_idbfs_loaded(int ok) {
  g_idbfs_ok.store(ok != 0, std::memory_order_release);
  g_idbfs_load_done.store(true, std::memory_order_release);
}

EMSCRIPTEN_KEEPALIVE void* selfano_alloc_bytes(int size) {
  if (size <= 0) {
    return nullptr;
  }
  std::lock_guard lock(g_web_import_mutex);
  g_web_staging_buffers.emplace_back(static_cast<std::size_t>(size));
  return g_web_staging_buffers.back().data();
}

EMSCRIPTEN_KEEPALIVE void selfano_free_bytes(void* ptr) {
  if (ptr == nullptr) {
    return;
  }
  std::lock_guard lock(g_web_import_mutex);
  std::erase_if(g_web_staging_buffers,
                [ptr](const std::vector<std::uint8_t>& buf) { return buf.data() == ptr; });
}

EMSCRIPTEN_KEEPALIVE void selfano_on_imported_file(const char* filename, const std::uint8_t* data,
                                                   int len) {
  if (filename == nullptr || data == nullptr || len <= 0) {
    return;
  }
  std::lock_guard lock(g_web_import_mutex);
  selfano::app::WebImportedFile item;
  item.filename = filename;
  item.bytes.assign(data, data + len);
  g_web_import_queue.push_back(std::move(item));
}

} // extern "C"

namespace selfano::app {

int browser_viewport_width() {
  return selfano_browser_viewport_width();
}

int browser_viewport_height() {
  return selfano_browser_viewport_height();
}

void idbfs_mount_and_load() {
  selfano_idbfs_mount_and_load();
}

bool idbfs_load_done() noexcept {
  return g_idbfs_load_done.load(std::memory_order_acquire);
}

void idbfs_sync_to_disk() {
  selfano_idbfs_sync_to_disk();
}

void web_open_file_picker() {
  selfano_ensure_send_helper_js();
  selfano_web_open_file_picker();
}

void web_install_drop_zone() {
  selfano_ensure_send_helper_js();
  selfano_web_install_drop_zone();
}

std::vector<WebImportedFile> drain_web_imported_files() {
  std::vector<WebImportedFile> out;
  std::lock_guard lock(g_web_import_mutex);
  out.swap(g_web_import_queue);
  return out;
}

} // namespace selfano::app

#else

namespace selfano::app {

int browser_viewport_width() {
  return 1360;
}

int browser_viewport_height() {
  return 860;
}

void idbfs_mount_and_load() {
}

bool idbfs_load_done() noexcept {
  return true;
}

void idbfs_sync_to_disk() {
}

void web_open_file_picker() {
}

void web_install_drop_zone() {
}

std::vector<WebImportedFile> drain_web_imported_files() {
  return {};
}

} // namespace selfano::app

#endif

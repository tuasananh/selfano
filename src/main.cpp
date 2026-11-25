#include "app/app_controller.hpp"
#include "app/cli_options.hpp"

#include <cstdio>
#include <exception>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

#ifdef __EMSCRIPTEN__
std::unique_ptr<selfano::app::AppController> g_web_app;

void emscripten_tick_trampoline(void* arg) {
  auto* app = static_cast<selfano::app::AppController*>(arg);
  if (app != nullptr) {
    app->tick();
  }
}
#endif

} // namespace

int main(int argc, char* argv[]) {
  try {
    const selfano::app::CliOptions cli = selfano::app::parse_cli(argc, argv);

#ifdef __EMSCRIPTEN__
    g_web_app = std::make_unique<selfano::app::AppController>();
    if (!g_web_app->init(cli)) {
      g_web_app.reset();
      return 1;
    }
    emscripten_set_main_loop_arg(&emscripten_tick_trampoline, g_web_app.get(), 0, false);
    return 0;
#else
    auto app = std::make_unique<selfano::app::AppController>();
    if (!app->init(cli)) {
      return 1;
    }
    while (!app->is_done()) {
      app->tick();
    }
    app->shutdown();
    return 0;
#endif
  } catch (const std::exception& e) {
    std::fputs("Fatal error: ", stderr);
    std::fputs(e.what(), stderr);
    std::fputc('\n', stderr);
    return 1;
  } catch (...) {
    std::fputs("Fatal error: unknown exception\n", stderr);
    return 1;
  }
}

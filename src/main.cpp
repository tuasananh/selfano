#include "midi/midi_engine_stub.hpp"

#include "ui/font_loader.hpp"
#include "ui/staff_showcase.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct CliOptions {
  int smoke_frames{0};
  std::string screenshot_path;
};

[[nodiscard]] CliOptions parse_cli(int argc, char* argv[]) {
  CliOptions opts{};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--smoke-frames" && i + 1 < argc) {
      opts.smoke_frames = std::max(1, std::atoi(argv[++i]));
    } else if (arg == "--screenshot" && i + 1 < argc) {
      opts.screenshot_path = argv[++i];
    }
  }
  return opts;
}

} // namespace

int main(int argc, char* argv[]) {
  const CliOptions cli = parse_cli(argc, argv);
  (void)selfano::midi::midi_subsystem_ready();

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "Error: SDL_Init(): " << SDL_GetError() << '\n';
    return 1;
  }

  float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (main_scale <= 0.0f) {
    main_scale = 1.0f;
  }

  const SDL_WindowFlags window_flags =
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  SDL_Window* window =
      SDL_CreateWindow("Selfano — C++23 Piano Trainer", static_cast<int>(1360 * main_scale),
                       static_cast<int>(820 * main_scale), window_flags);
  if (window == nullptr) {
    std::cerr << "Error: SDL_CreateWindow(): " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer == nullptr) {
    std::cerr << "Error: SDL_CreateRenderer(): " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark();
  style.FrameRounding = 4.0f;
  style.WindowRounding = 0.0f;
  style.ScaleAllSizes(main_scale);
  style.FontScaleDpi = main_scale;

  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  const auto fonts = selfano::ui::load_app_fonts(io, 16.0f);
  if (!fonts.has_value()) {
    std::cerr << "Error loading fonts: " << fonts.error() << '\n';
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  selfano::ui::StaffShowcase showcase;
  const ImVec4 clear_color(0.08f, 0.09f, 0.12f, 1.0f);

  bool done = false;
  int rendered_frames = 0;
  bool screenshot_saved = false;

  while (!done) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        done = true;
      } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window)) {
        done = true;
      }
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    showcase.draw(fonts->smufl_font, fonts->ui_font, fonts->loaded_smufl_path.c_str());

    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z,
                                clear_color.w);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    ++rendered_frames;
    const bool should_capture = !cli.screenshot_path.empty() && !screenshot_saved &&
                                ((cli.smoke_frames > 0 && rendered_frames >= cli.smoke_frames) ||
                                 (cli.smoke_frames == 0 && rendered_frames >= 3));
    if (should_capture) {
      SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
      if (surface != nullptr) {
        if (!SDL_SaveBMP(surface, cli.screenshot_path.c_str())) {
          std::cerr << "Error saving screenshot: " << SDL_GetError() << '\n';
        }
        SDL_DestroySurface(surface);
      } else {
        std::cerr << "Error reading pixels: " << SDL_GetError() << '\n';
      }
      screenshot_saved = true;
    }

    SDL_RenderPresent(renderer);

    if (cli.smoke_frames > 0 && rendered_frames >= cli.smoke_frames) {
      done = true;
    }
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}

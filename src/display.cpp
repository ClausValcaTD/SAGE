// SDL2 window for the SAGE video output.
//
// The whole file compiles to stubs when SAGE_HAS_SDL is not defined, so the
// emulator builds and runs headless in environments without SDL2 (CI, the
// bring-up container). The window is a pure scan-out sink: all rendering lives
// in Video::render().

#include "sage/display.h"

#ifdef SAGE_HAS_SDL
#include <SDL.h>
#endif

namespace sage {

#ifdef SAGE_HAS_SDL

Display::~Display() { close(); }

bool Display::open(const char* title, int width, int height) {
    if (m_open)
        return true;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return false;

    SDL_Window* window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          width, height, SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)  // no GPU (headless container, VNC server, ...) - use the CPU blitter
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    m_window = window;
    m_renderer = renderer;
    m_texture = texture;
    m_width = width;
    m_height = height;
    m_last_frame_ms = SDL_GetTicks64();
    m_open = true;
    return true;
}

void Display::close() {
    if (m_texture)
        SDL_DestroyTexture(static_cast<SDL_Texture*>(m_texture));
    if (m_renderer)
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(m_renderer));
    if (m_window)
        SDL_DestroyWindow(static_cast<SDL_Window*>(m_window));
    m_texture = m_renderer = m_window = nullptr;
    if (m_open)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    m_open = false;
}

bool Display::pump() {
    if (!m_open)
        return false;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
            return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
            return false;
    }
    return true;
}

void Display::present(const std::uint32_t* rgba) {
    if (!m_open)
        return;
    SDL_UpdateTexture(static_cast<SDL_Texture*>(m_texture), nullptr, rgba, m_width * 4);
    SDL_RenderClear(static_cast<SDL_Renderer*>(m_renderer));
    SDL_RenderCopy(static_cast<SDL_Renderer*>(m_renderer), static_cast<SDL_Texture*>(m_texture),
                   nullptr, nullptr);
    SDL_RenderPresent(static_cast<SDL_Renderer*>(m_renderer));
}

void Display::pace_frame() {
    if (!m_open) {
        m_last_frame_ms = 0;
        return;
    }
    constexpr std::uint64_t kTargetMs = 1000 / 60;
    const std::uint64_t now = SDL_GetTicks64();
    const std::uint64_t elapsed = now - m_last_frame_ms;
    if (elapsed < kTargetMs)
        SDL_Delay(static_cast<std::uint32_t>(kTargetMs - elapsed));
    const std::uint64_t end = SDL_GetTicks64();
    m_last_frame_ms = end;
}

#else  // !SAGE_HAS_SDL

Display::~Display() { close(); }

bool Display::open(const char*, int, int) { return false; }
void Display::close() { m_open = false; }
bool Display::pump() { return false; }
void Display::present(const std::uint32_t*) {}
void Display::pace_frame() { m_last_frame_ms = 0; }

#endif  // SAGE_HAS_SDL

}  // namespace sage

#pragma once

#include <cstdint>

namespace sage {

// Thin SDL2 window that scans out the video subsystem's RGBA frame.
//
// Compiled in only when SDL2 is found (SAGE_HAS_SDL); otherwise every method is
// a no-op that reports a closed window, so the emulator still runs headless.
class Display {
public:
    Display() = default;
    ~Display();

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    // Opens a window and streaming texture of `width` x `height`.
    // Returns false if SDL is unavailable or initialisation fails.
    bool open(const char* title, int width, int height);
    void close();

    bool is_open() const { return m_open; }

    // Pumps pending events. Returns false once the user has requested quit.
    bool pump();

    // Uploads an RGBA8888 buffer and presents it.
    void present(const std::uint32_t* rgba);

    // Blocks until the next 1/60 s boundary, pacing emulation to real time.
    void pace_frame();

    // Milliseconds elapsed since the last pace_frame() call.
    std::uint64_t last_frame_ms() const { return m_last_frame_ms; }

private:
    bool m_open = false;
    int m_width = 0;
    int m_height = 0;
    std::uint64_t m_last_frame_ms = 0;
    void* m_window = nullptr;
    void* m_renderer = nullptr;
    void* m_texture = nullptr;
};

}  // namespace sage

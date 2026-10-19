#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct SDL_Window;
union SDL_Event;

class Window
{
public:
    Window() = default;
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool create(const char *title, int width, int height);
    void destroy();

    bool pollEvents();

    using EventCallback = std::function<void(const SDL_Event &)>;
    void setEventCallback(EventCallback callback) { m_eventCallback = std::move(callback); };

    bool wasResized() const { return m_resized; };

    uint32_t getWidth() const { return m_width; };
    uint32_t getHeight() const { return m_height; };

    float getTime() const;

    std::string getBasePath() const;

    void *getNativeWindowHandle() const { return m_nativeWindow; };
    void *getNativeDisplayHandle() const { return m_nativeDisplay; };
    bool isWayland() const { return m_isWayland; };

    SDL_Window *getSDLWindow() const { return m_window; };

private:
    bool queryNativeHandles();

    SDL_Window *m_window = nullptr;
    EventCallback m_eventCallback;
    void *m_metalView = nullptr;

    void *m_nativeWindow = nullptr;
    void *m_nativeDisplay = nullptr;
    bool m_isWayland = false;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint64_t m_startTicks = 0;
    bool m_resized = false;
};
#include "core/Window.hpp"

#include <SDL3/SDL.h>
#if defined(SDL_PLATFORM_MACOS)
#include <SDL3/SDL_metal.h>
#endif

#include <cstdio>
#include <cstring>

Window::~Window()
{
    destroy();
}

bool Window::create(const char *title, int width, int height)
{
    if (m_window)
        return true;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "Window: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    m_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (!m_window)
    {
        std::fprintf(stderr, "Window: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    m_width = uint32_t(w);
    m_height = uint32_t(h);

    if (!queryNativeHandles())
    {
        std::fprintf(stderr, "Window: Could not query native handles\n");
        destroy();
        return false;
    }

    m_startTicks = SDL_GetTicks();
    return true;
}

void Window::destroy()
{
    if (!m_window)
        return;

#if defined(SDL_PLATFORM_MACOS)
    if (m_metalView)
        SDL_Metal_DestroyView(static_cast<SDL_MetalView>(m_metalView));
#endif

    m_metalView = nullptr;
    m_nativeWindow = nullptr;
    m_nativeDisplay = nullptr;

    SDL_DestroyWindow(m_window);
    m_window = nullptr;
    SDL_Quit();
}

bool Window::pollEvents()
{
    m_resized = false;
    bool keepRunning = true;

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (m_eventCallback)
            m_eventCallback(e);

        switch (e.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            keepRunning = false;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            m_width = uint32_t(e.window.data1);
            m_height = uint32_t(e.window.data2);
            m_resized = true;
            break;
        default:
            break;
        }
    }

    return keepRunning;
}

float Window::getTime() const
{
    return float(SDL_GetTicks() - m_startTicks) / 1000.0f;
}

std::string Window::getBasePath() const
{
    const char *base = SDL_GetBasePath();
    return base ? std::string(base) : std::string();
}

bool Window::queryNativeHandles()
{
    SDL_PropertiesID props = SDL_GetWindowProperties(m_window);

#if defined(SDL_PLATFORM_WIN32)
    m_nativeWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS)
    (void)props;
    SDL_MetalView view = SDL_Metal_CreateView(m_window);
    m_metalView = view;
    m_nativeWindow = SDL_Metal_GetLayer(view);
#elif defined(SDL_PLATFORM_LINUX)
    const char *driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "x11") == 0)
    {
        m_nativeDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        m_nativeWindow = (void *)(uintptr_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    }
    else if (driver && std::strcmp(driver, "wayland") == 0)
    {
        m_nativeDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        m_nativeWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        m_isWayland = true;
    }
#endif

    return m_nativeWindow != nullptr;
}
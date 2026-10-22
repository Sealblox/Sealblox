#include <iostream>
#include <core/Window.hpp>
#include <core/Renderer.hpp>

#include <entt/entt.hpp>
#include <ecosystem/ModelRelated.hpp>
#include <ecosystem/Camera.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

int main(int, char **)
{
    entt::registry registry;

    Window window;
    if (!window.create("Sealblox", 1280, 720))
        return 1;

    Renderer renderer;
    if (!renderer.init(window))
        return 1;

    ECS::Camera camera;
    camera.setViewport(window.getWidth(), window.getHeight());
    camera.yaw = bx::toRad(30.0f);
    camera.pitch = bx::toRad(-20.0f);

    ECS::CameraController cameraController(camera, window.getSDLWindow());
    cameraController.focus({0.0f, 0.0f, 0.0f}, 10.0f, true);
    float lastTime = window.getTime();

    window.setEventCallback([&](const SDL_Event &e)
                            { cameraController.handleEvent(e); });

    while (window.pollEvents())
    {
        const float now = window.getTime();
        const float dt = bx::min(now - lastTime, 0.1f);

        cameraController.update(dt, std::optional<bx::Vec3>(), std::optional<bx::Vec3>());

        bgfx::setViewTransform(0, camera.getView(), camera.getProjection());
        renderer.beginFrame();

        if (window.wasResized())
        {
            renderer.resize(window.getWidth(), window.getHeight());
        }

        float matrix[16];
        bx::mtxIdentity(matrix);

        bx::mtxRotateXYZ(matrix, 0, 45, 0);

        renderer.drawCube(matrix);
        renderer.endFrame();
    }

    return 0;
}
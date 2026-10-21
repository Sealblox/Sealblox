#include <iostream>
#include <core/Window.hpp>
#include <core/Renderer.hpp>

#include <entt/entt.hpp>
#include <ecosystem/ModelRelated.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

int main(int, char **)
{
    Window window;
    if (!window.create("Sealblox", 1280, 720))
        return 1;

    Renderer renderer;
    if (!renderer.init(window))
        return 1;

    float view[16];
    float proj[16];

    bx::mtxLookAt(
        view,
        bx::Vec3(0.0f, 2.5f, -5.0f), // Camera position
        bx::Vec3(0.0f, 0.0f, 0.0f)   // Look at the origin
    );

    bx::mtxProj(
        proj,
        60.0f,
        static_cast<float>(window.getWidth()) /
            static_cast<float>(window.getHeight()),
        0.1f,
        100.0f,
        bgfx::getCaps()->homogeneousDepth);

    bgfx::setViewTransform(0, view, proj);

    while (window.pollEvents())
    {
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
#pragma once

#include <bx/math.h>
#include <cstdint>
#include <entt/entt.hpp>

struct SDL_Window;
union SDL_Event;

namespace ECS
{
    struct Instance;

    enum class ProjectionType
    {
        Perspective,
        Orthographic
    };

    struct Ray
    {
        bx::Vec3 origin = {0.0f, 0.0f, 0.0f};
        bx::Vec3 direction = {0.0f, 0.0f, 1.0f};
        float distance = 1.0f;
    };

    struct RayResult
    {
        bx::Vec3 origin = {0.0f, 0.0f, 0.0f};
        bx::Vec3 direction = {0.0f, 0.0f, 1.0f};

        Instance &instance;

        Instance &getCollidedInstance()
        {
            return instance;
        }
    };

    class Camera
    {
    public:
        Camera();

        bx::Vec3 position = {0.0f, 2.5f, -6.0f};
        bx::Vec3 focusPoint = {0.0f, 0.0f, 0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;

        ProjectionType projection = ProjectionType::Perspective;
        float fovY = 60.0f;
        float orthoHeight = 10.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;

        void setViewport(uint32_t width, uint32_t height);
        uint32_t getWidth() const { return m_width; }
        uint32_t getHeight() const { return m_height; }
        float getAspect() const { return float(m_width) / float(m_height); }

        bx::Vec3 getForward() const;
        bx::Vec3 getRight() const;
        bx::Vec3 getUp() const;

        void lookAt(const bx::Vec3 &target);

        void updateMatrices();

        const float *getView() const { return m_view; }
        const float *getProjection() const { return m_proj; }
        const float *getViewProjection() const { return m_viewProj; }

        Ray screenPointToRay(float x, float y) const;

        static bx::Vec3 directionFromAngles(float yaw, float pitch);

    private:
        float m_view[16];
        float m_proj[16];
        float m_viewProj[16];
        float m_invViewProj[16];

        uint32_t m_width = 1;
        uint32_t m_height = 1;
        bool m_homogeneousDepth = false;
    };

    class CameraController
    {
    public:
        enum class Mode
        {
            Follow,
            Scripted
        };

        struct Settings
        {
            float moveSpeed = 5.0f;
            float boostMultiplier = 4.0f;
            float lookSensitivity = 0.0035f;
            float panSpeed = 0.0015f;
            float zoomStep = 0.1f;
            float minDistance = 0.25f;
            float maxDistance = 500.0f;
            float smoothing = 18.0f;
            float shiftLockOffset = 1.75f;
            bool invertY = false;
            bool moveOnlyWhileLooking = false;
        };

        CameraController(Camera &camera, SDL_Window *window);

        void handleEvent(const SDL_Event &e);

        void update(float dt, std::optional<bx::Vec3> pos, std::optional<bx::Vec3> rot);

        void setMode(Mode mode);
        Mode getMode() const { return m_mode; }

        void focus(const bx::Vec3 &target, float distance, bool immediate = false);

        void setShiftLock(bool enabled);
        bool isShiftLocked() const { return m_shiftLock; }
        void toggleShiftLock() { setShiftLock(!m_shiftLock); }

        bx::Vec3 getFollowTarget() const { return m_target; }
        float getFollowDistance() const { return m_distance; }

        Settings settings;

    private:
        void syncFromCamera();
        void releaseInput();
        void updateMouseCapture();
        void updateFollow(float dx, float dy, float wheel, float blend);
        void updateScripted(bx::Vec3 pos, bx::Vec3 rot);

        Camera &m_camera;
        SDL_Window *m_window = nullptr;
        Mode m_mode = Mode::Follow;

        float m_goalYaw = 0.0f;
        float m_goalPitch = 0.0f;
        bx::Vec3 m_goalPosition = {0.0f, 0.0f, 0.0f};
        bx::Vec3 m_goalTarget = {0.0f, 0.0f, 0.0f};
        float m_goalDistance = 6.0f;

        bx::Vec3 m_target = {0.0f, 0.0f, 0.0f};
        float m_distance = 6.0f;

        float m_mouseDX = 0.0f;
        float m_mouseDY = 0.0f;
        float m_wheel = 0.0f;

        bool m_looking = false;
        bool m_orbiting = false;
        bool m_panning = false;
        uint8_t m_panButton = 0;
        bool m_mouseCaptured = false;

        bool m_shiftLock = false;
        float m_shoulderOffset = 0.0f;
    };
}
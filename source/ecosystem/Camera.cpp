#include <ecosystem/Camera.hpp>

#include <bgfx/bgfx.h>
#include <SDL3/SDL.h>
#include <optional>

namespace ECS
{
    const bx::Vec3 kWorldUp = {0.0f, 1.0f, 0.0f};
    const float kMaxPitch = bx::kPiHalf - 0.01f;

    float clampPitch(float pitch)
    {
        return bx::clamp(pitch, -kMaxPitch, kMaxPitch);
    }

    Camera::Camera()
    {
        bx::mtxIdentity(m_view);
        bx::mtxIdentity(m_proj);
        bx::mtxIdentity(m_viewProj);
        bx::mtxIdentity(m_invViewProj);
    }

    void Camera::setViewport(uint32_t width, uint32_t height)
    {
        m_width = width > 0 ? width : 1;
        m_height = height > 0 ? height : 1;
    }

    bx::Vec3 Camera::directionFromAngles(float yaw, float pitch)
    {
        const float cp = bx::cos(pitch);
        return {cp * bx::sin(yaw), bx::sin(pitch), cp * bx::cos(yaw)};
    }

    bx::Vec3 Camera::getForward() const
    {
        return directionFromAngles(yaw, pitch);
    }

    bx::Vec3 Camera::getRight() const
    {
        return bx::normalize(bx::cross(kWorldUp, getForward()));
    }

    bx::Vec3 Camera::getUp() const
    {
        return bx::cross(getForward(), getRight());
    }

    void Camera::lookAt(const bx::Vec3 &target)
    {
        const bx::Vec3 delta = bx::sub(target, position);
        if (bx::length(delta) < 1e-6f)
            return;

        const bx::Vec3 dir = bx::normalize(delta);
        yaw = bx::atan2(dir.x, dir.z);
        pitch = clampPitch(bx::asin(dir.y));
    }

    void Camera::updateMatrices()
    {
        const bgfx::Caps *caps = bgfx::getCaps();
        m_homogeneousDepth = caps && caps->homogeneousDepth;

        const bx::Vec3 at = bx::add(position, getForward());
        bx::mtxLookAt(m_view, position, at, kWorldUp);

        if (projection == ProjectionType::Perspective)
        {
            bx::mtxProj(m_proj, fovY, getAspect(), nearPlane, farPlane, m_homogeneousDepth);
        }
        else
        {
            const float halfH = orthoHeight * 0.5f;
            const float halfW = halfH * getAspect();
            bx::mtxOrtho(m_proj, -halfW, halfW, -halfH, halfH, nearPlane, farPlane, 0.0f,
                         m_homogeneousDepth);
        }

        bx::mtxMul(m_viewProj, m_view, m_proj);
        bx::mtxInverse(m_invViewProj, m_viewProj);
    }

    Ray Camera::screenPointToRay(float x, float y) const
    {
        const float ndcX = 2.0f * x / float(m_width) - 1.0f;
        const float ndcY = 1.0f - 2.0f * y / float(m_height);
        const float ndcNear = m_homogeneousDepth ? -1.0f : 0.0f;

        const bx::Vec3 pNear = bx::mulH({ndcX, ndcY, ndcNear}, m_invViewProj);
        const bx::Vec3 pFar = bx::mulH({ndcX, ndcY, 1.0f}, m_invViewProj);

        Ray ray;
        ray.origin = pNear;
        ray.direction = bx::normalize(bx::sub(pFar, pNear));
        return ray;
    }

    CameraController::CameraController(Camera &camera, SDL_Window *window)
        : m_camera(camera), m_window(window)
    {
        syncFromCamera();

        m_target = m_camera.focusPoint;
        m_camera.position = bx::mad(m_camera.getForward(), -m_distance, m_target);
    }

    void CameraController::syncFromCamera()
    {
        m_goalYaw = m_camera.yaw;
        m_goalPitch = clampPitch(m_camera.pitch);
        m_goalPosition = m_camera.position;

        const float distance = bx::length(bx::sub(m_camera.focusPoint, m_camera.position));
        m_goalDistance = bx::clamp(distance, settings.minDistance, settings.maxDistance);
        m_distance = m_goalDistance;

        m_target = bx::mad(m_camera.getForward(), m_distance, m_camera.position);
    }

    void CameraController::setMode(Mode mode)
    {
        if (mode == m_mode)
            return;

        releaseInput();
        m_mode = mode;
        updateMouseCapture();

        syncFromCamera();
    }

    void CameraController::focus(const bx::Vec3 &target, float distance, bool immediate)
    {
        m_camera.focusPoint = target;

        if (m_mode != Mode::Follow)
        {
            releaseInput();
            m_mode = Mode::Follow;
            updateMouseCapture();
            syncFromCamera();
        }

        m_goalDistance = bx::clamp(distance, settings.minDistance, settings.maxDistance);

        if (immediate)
        {
            m_target = m_camera.focusPoint;
            m_distance = m_goalDistance;
            m_camera.yaw = m_goalYaw;
            m_camera.pitch = m_goalPitch;
            m_camera.position = bx::mad(m_camera.getForward(), -m_distance, m_target);
        }
    }

    void CameraController::setShiftLock(bool enabled)
    {
        m_shiftLock = enabled;
        updateMouseCapture();
    }

    void CameraController::handleEvent(const SDL_Event &e)
    {
        switch (e.type)
        {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (m_mode == Mode::Scripted)
            {
            }
            else
            {
                const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    m_orbiting = true;
                }
            }
            updateMouseCapture();
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_RIGHT)
                m_looking = false;
            if (e.button.button == SDL_BUTTON_RIGHT)
                m_orbiting = false;
            updateMouseCapture();
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (m_looking || m_orbiting || m_panning || m_shiftLock)
            {
                m_mouseDX += e.motion.xrel;
                m_mouseDY += e.motion.yrel;
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            m_wheel += (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -e.wheel.y : e.wheel.y;
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            releaseInput();
            break;

        default:
            break;
        }
    }

    void CameraController::releaseInput()
    {
        m_looking = m_orbiting = m_panning = false;
        m_mouseDX = m_mouseDY = m_wheel = 0.0f;
        updateMouseCapture();
    }

    void CameraController::updateMouseCapture()
    {
        const bool want = m_looking || m_orbiting || m_panning || (m_shiftLock && m_mode == Mode::Follow);
        if (want == m_mouseCaptured || !m_window)
            return;

        SDL_SetWindowRelativeMouseMode(m_window, want);
        m_mouseCaptured = want;
    }

    void CameraController::update(float dt, std::optional<bx::Vec3> pos, std::optional<bx::Vec3> rot)
    {
        const float dx = m_mouseDX;
        const float dy = m_mouseDY * (settings.invertY ? -1.0f : 1.0f);
        const float wheel = m_wheel;
        m_mouseDX = m_mouseDY = m_wheel = 0.0f;

        const float blend = settings.smoothing > 0.0f ? 1.0f - bx::exp(-settings.smoothing * dt) : 1.0f;

        if (m_mode == Mode::Scripted && pos.has_value() && rot.has_value())
            updateScripted(pos.value(), rot.value());
        else
            updateFollow(dx, dy, wheel, blend);

        m_camera.updateMatrices();
    }

    void CameraController::updateScripted(bx::Vec3 pos, bx::Vec3 rot)
    {
    }

    void CameraController::updateFollow(float dx, float dy, float wheel, float blend)
    {
        if (m_orbiting || m_shiftLock)
        {
            m_goalYaw += dx * settings.lookSensitivity;
            m_goalPitch = clampPitch(m_goalPitch - dy * settings.lookSensitivity);
        }

        if (m_panning)
        {
            const float scale = settings.panSpeed * m_distance;
            m_camera.focusPoint = bx::mad(m_camera.getRight(), -dx * scale, m_camera.focusPoint);
            m_camera.focusPoint = bx::mad(m_camera.getUp(), dy * scale, m_camera.focusPoint);
        }

        if (wheel != 0.0f)
        {
            m_goalDistance *= bx::pow(1.0f - settings.zoomStep, wheel);
            m_goalDistance = bx::clamp(m_goalDistance, settings.minDistance, settings.maxDistance);
        }

        m_camera.yaw = bx::lerp(m_camera.yaw, m_goalYaw, blend);
        m_camera.pitch = bx::lerp(m_camera.pitch, m_goalPitch, blend);
        m_target = bx::lerp(m_target, m_camera.focusPoint, blend);
        m_distance = bx::lerp(m_distance, m_goalDistance, blend);
        m_shoulderOffset = bx::lerp(m_shoulderOffset, m_shiftLock ? settings.shiftLockOffset : 0.0f, blend);

        m_camera.position = bx::mad(m_camera.getForward(), -m_distance, m_target);
        m_camera.position = bx::mad(m_camera.getRight(), m_shoulderOffset, m_camera.position);

        m_camera.orthoHeight = 2.0f * m_distance * bx::tan(bx::toRad(m_camera.fovY) * 0.5f);
    }
}
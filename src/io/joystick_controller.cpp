#include "io/joystick_controller.h"

namespace fh {


void JoystickController::SetConfig(int cx, int cy, int areaR, int thumbR, int levels) {
    m_centerX = cx;
    m_centerY = cy;
    m_areaRadius = areaR;
    m_thumbRadius = thumbR;
    m_levels = levels;
    m_state = {};
}

void JoystickController::ProcessTouchDown(int x, int y) {
    m_state.isDown = true;
    m_lastDirection = -1;
    m_lastDistance = 0.0f;
    UpdateState(x, y);
}

void JoystickController::ProcessTouchMove(int x, int y) {
    if (!m_state.isDown) return;
    UpdateState(x, y);
}

void JoystickController::ProcessTouchUp() {
    m_state.isDown = false;
    m_state.angle = 0.0f;
    m_state.distance = 0.0f;
    m_state.direction = (int)Direction::CENTER;
    m_state.level = 0;
    m_state.thumbX = m_centerX;
    m_state.thumbY = m_centerY;
    m_lastDirection = -1;
    m_lastDistance = 0.0f;
    if (m_callback) m_callback(m_state);
}

void JoystickController::UpdateState(int rawX, int rawY) {
    float dx = (float)(rawX - m_centerX);
    float dy = (float)(rawY - m_centerY);
    float dist = std::sqrt(dx * dx + dy * dy);

    float range = (float)(m_areaRadius - m_thumbRadius);
    if (range < 1.0f) range = 1.0f;

    float angle = CalcAngle(dx, dy);
    float thumbX = (float)m_centerX;
    float thumbY = (float)m_centerY;

    if (dist > range) {
        float ratio = range / dist;
        thumbX = m_centerX + dx * ratio;
        thumbY = m_centerY + dy * ratio;
    } else {
        thumbX = (float)rawX;
        thumbY = (float)rawY;
    }

    float normalizedDist = std::min(dist / range, 1.0f);

    m_state.angle = angle;
    m_state.distance = normalizedDist;
    m_state.direction = ClassifyDirection(angle);
    m_state.level = CalcLevel(normalizedDist);
    m_state.thumbX = (int)thumbX;
    m_state.thumbY = (int)thumbY;

    if (m_callback) m_callback(m_state);
}

float JoystickController::CalcAngle(float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f) return 0.0f;
    float rad = std::atan2(dy, dx);
    float deg = rad * 180.0f / (float)M_PI;
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

int JoystickController::ClassifyDirection(float angle) {
    // 8-way classification (only mode supported)
    if (angle <= 22.5f || angle > 337.5f) return (int)Direction::RIGHT;
    if (angle <= 67.5f)  return (int)Direction::DOWN_RIGHT;
    if (angle <= 112.5f) return (int)Direction::DOWN;
    if (angle <= 157.5f) return (int)Direction::DOWN_LEFT;
    if (angle <= 202.5f) return (int)Direction::LEFT;
    if (angle <= 247.5f) return (int)Direction::UP_LEFT;
    if (angle <= 292.5f) return (int)Direction::UP;
    return (int)Direction::UP_RIGHT;
}

int JoystickController::CalcLevel(float distance) {
    return (int)(distance * m_levels);
}

}  // namespace fh

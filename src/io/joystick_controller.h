#ifndef JOYSTICK_CONTROLLER_H
#define JOYSTICK_CONTROLLER_H

#include <cmath>
#include <functional>

namespace fh {


struct JoystickState {
    float angle = 0.0f;       // [0, 360), 0=right, CCW
    float distance = 0.0f;    // normalized [0, 1]
    int direction = -1;       // Direction enum
    int level = 0;            // distance level [0, N]
    int thumbX = 0, thumbY = 0; // pixel position
    bool isDown = false;
};

enum class Direction {
    CENTER = -1,
    RIGHT = 0, UP = 1, LEFT = 2, DOWN = 3,
    UP_RIGHT = 4, UP_LEFT = 5, DOWN_LEFT = 6, DOWN_RIGHT = 7
};

class JoystickController {
public:
    void SetConfig(int centerX, int centerY, int areaRadius, int thumbRadius, int levels = 10);
    void SetCallback(std::function<void(const JoystickState&)> cb) { m_callback = std::move(cb); }

    void ProcessTouchDown(int x, int y);
    void ProcessTouchMove(int x, int y);
    void ProcessTouchUp();

    const JoystickState& GetState() const { return m_state; }
    int GetCenterX() const { return m_centerX; }
    int GetCenterY() const { return m_centerY; }
    int GetAreaRadius() const { return m_areaRadius; }
    int GetThumbRadius() const { return m_thumbRadius; }

private:
    void UpdateState(int rawX, int rawY);
    float CalcAngle(float dx, float dy);
    int ClassifyDirection(float angle);
    int CalcLevel(float distance);

    int m_centerX = 0, m_centerY = 0;
    int m_areaRadius = 200;
    int m_thumbRadius = 80;
    int m_levels = 10;
    JoystickState m_state;
    std::function<void(const JoystickState&)> m_callback;
    int m_lastDirection = -1;
    float m_lastDistance = 0.0f;
};


}  // namespace fh

#endif

#pragma once
#include <Arduino.h>
#include "Config.h"

enum class InclineState { IDLE, HOMING, MOVING_UP, MOVING_DOWN };

class Incline {
public:
    Incline(uint8_t pinUp = INCLINE_UP_PIN, uint8_t pinDown = INCLINE_DOWN_PIN);

    void begin();
    void update();              // chamar no loop principal

    void startHoming();         // desce até fim de curso → zera estimativa
    void setTargetDegrees(float deg);

    float getCurrentDegrees() const { return _currentDeg; }
    float getTargetDegrees()  const { return _targetDeg; }
    bool  isHomingDone()      const { return _homingDone; }
    bool  isBusy()            const { return _state != InclineState::IDLE; }
    InclineState getState()   const { return _state; }

private:
    uint8_t      _pinUp, _pinDown;
    InclineState _state       = InclineState::IDLE;
    float        _currentDeg  = 0.0f;
    float        _targetDeg   = 0.0f;
    bool         _homingDone  = false;

    unsigned long _moveStartMs  = 0;
    float         _degAtMoveStart = 0.0f;

    void relayOff();
    void startMove(InclineState dir);
    void updateEstimate();
};

#pragma once
#include <Arduino.h>
#include <PID_v1.h>
#include <Preferences.h>
#include "Config.h"
#include "SpeedSensor.h"

enum class FaultCode : uint8_t {
    NONE              = 0,
    HEARTBEAT_TIMEOUT = 1,
    ENCODER_LOSS      = 2,   // encoder óptico sem sinal (malha fechada)
};

class Motor {
public:
    Motor(SpeedSensor& sensor);

    void  begin();
    void  update();               // chamar no loop principal (< 10 ms)

    void  start();
    void  stop();                 // desaceleração normal
    void  emergencyStop();        // desaceleração rápida (sem fault)
    void  faultStop(FaultCode code); // parada de emergência + seta fault

    void  setTargetSpeed(float kmh);
    float getTargetSpeed()   const { return _targetSpeedKmh; }
    float getCurrentPwmPct() const { return _currentPwmPct; }
    bool  isRunning()        const { return _running; }

    void  setPidGains(float kp, float ki, float kd);
    void  savePidToNvs();
    float getPidKp() const { return _kp; }
    float getPidKi() const { return _ki; }
    float getPidKd() const { return _kd; }

    FaultCode getFault()     const { return _fault; }
    bool      hasFault()     const { return _fault != FaultCode::NONE; }
    void      clearFault()         { _fault = FaultCode::NONE; }

    // Chamado por WebSocketHandler ou BLEManager ao receber heartbeat de qualquer cliente.
    // Motor dá faultStop(HEARTBEAT_TIMEOUT) se nenhum cliente enviar em WS_HEARTBEAT_TIMEOUT_MS.
    void resetHeartbeat();
    bool isHeartbeatOk() const;

private:
    SpeedSensor& _sensor;

    // br3ttb/PID exige ponteiros para double
    double _pidSetpoint = 0.0;
    double _pidInput    = 0.0;
    double _pidOutput   = 0.0;
    PID    _pid;

    bool      _running          = false;
    bool      _stopping         = false;   // desaceleração controlada em andamento (malha fechada)
    FaultCode _fault            = FaultCode::NONE;
    float     _targetSpeedKmh   = 0.0f;
    float     _rampedSetpoint   = 0.0f;   // setpoint suavizado (km/h) — malha fechada
    float     _currentPwmPct    = 0.0f;
    float     _targetPwmPct     = 0.0f;
    float     _activeRampDown   = RAMP_RATE_DOWN;

    float       _kp = PID_KP;
    float       _ki = PID_KI;
    float       _kd = PID_KD;
    Preferences _prefs;

    unsigned long _lastUpdateMs   = 0;
    unsigned long _startedAtMs    = 0;   // para detecção de perda de encoder
    unsigned long _lastHeartbeatMs = 0;  // último heartbeat de qualquer interface

    void  applyPwmPct(float pct);
    float speedToPwmPct(float kmh) const;
    void  _loadPidFromNvs();
};

#pragma once
#include <Arduino.h>
#include <PID_v1.h>
#include <Preferences.h>
#include "Config.h"
#include "SpeedSensor.h"

enum class FaultCode : uint8_t {
    NONE                  = 0,
    HEARTBEAT_TIMEOUT     = 1,
    ENCODER_LOSS          = 2,   // reservado, não utilizado (encoder é só telemetria)
    OVERCURRENT           = 3,   // trip rápido — curto/falha
    OVERCURRENT_SUSTAINED = 4,   // trip lento — jam mecânico/sobrecarga térmica
    OVERSPEED             = 5,   // teto absoluto de velocidade (FCEM) excedido
    OVERSPEED_TARGET      = 6,   // velocidade muito acima do alvo comandado (malha fechada)
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

    // Alternância malha aberta/fechada em runtime — TEMPORÁRIO para testes de bancada.
    // Recusa a troca com o motor rodando (evita transição de controle no meio da operação).
    bool isOpenLoop() const { return _openLoop; }
    bool setOpenLoop(bool openLoop);

    // ─── Telemetria elétrica sensorless (FCEM) ──────────────────────────────────
    // "Raw" = volts pós-EMA, pré-escala (direto de analogReadMilliVolts/1000.0f).
    float getVBusRaw() const { return _vBusRawFilt; }
    float getVMotRaw() const { return _vMotRawFilt; }
    float getVCurRaw() const { return _vCurRawFilt; }
    float getVBus()    const { return _vBus; }
    float getVMot()    const { return _vMot; }
    float getCurrentA() const { return _iA; }
    float getFcem()    const { return _fcem; }
    float getSpeedFromFcemKmh() const;   // velocidade estimada a partir da FCEM (mesma relação linear usada no setpoint)

    // ─── Tuning em runtime — malha externa (velocidade/FCEM → I_ref) ────────────
    float getSpdKp() const { return _spdKp; }
    float getSpdKi() const { return _spdKi; }
    float getSpdKd() const { return _spdKd; }
    void  setSpdPidGains(float kp, float ki, float kd);
    void  saveSpdPidToNvs();

    // ─── Tuning em runtime — malha interna (corrente → duty%) ───────────────────
    float getCurKp() const { return _curKp; }
    float getCurKi() const { return _curKi; }
    float getCurKd() const { return _curKd; }
    void  setCurPidGains(float kp, float ki, float kd);
    void  saveCurPidToNvs();

    // ─── FCEM alvo em velocidade máxima — Ke sensorless, ajustável em bancada ──
    float getFcemMaxV() const { return _fcemMaxV; }
    void  setFcemMaxV(float v);
    void  saveFcemMaxToNvs();

    FaultCode getFault()     const { return _fault; }
    bool      hasFault()     const { return _fault != FaultCode::NONE; }
    void      clearFault()         { _fault = FaultCode::NONE; }

    // Chamado por WebSocketHandler ou BLEManager ao receber heartbeat de qualquer cliente.
    // Motor dá faultStop(HEARTBEAT_TIMEOUT) se nenhum cliente enviar em WS_HEARTBEAT_TIMEOUT_MS.
    void resetHeartbeat();
    bool isHeartbeatOk() const;

private:
    SpeedSensor& _sensor;   // mantido só para telemetria visual (RPM/velocidade) — NUNCA alimenta o PID

    // br3ttb/PID exige ponteiros para double — malha externa (FCEM → I_ref)
    double _pidSpdSetpoint = 0.0;
    double _pidSpdInput    = 0.0;
    double _pidSpdOutput   = 0.0;
    PID    _pidSpd;

    // br3ttb/PID exige ponteiros para double — malha interna (I_a → duty%)
    double _pidCurSetpoint = 0.0;
    double _pidCurInput    = 0.0;
    double _pidCurOutput   = 0.0;
    PID    _pidCur;

    bool      _running          = false;
    bool      _stopping         = false;   // desaceleração controlada em andamento (malha fechada)
    bool      _openLoop         = OPEN_LOOP_MODE;   // runtime — ver setOpenLoop()
    bool      _cascadeActive    = false;   // handoff bumpless da zona cega de FCEM (ver update())
    FaultCode _fault            = FaultCode::NONE;
    float     _targetSpeedKmh   = 0.0f;
    float     _rampedSetpoint   = 0.0f;   // setpoint suavizado (km/h) — malha fechada
    float     _currentPwmPct    = 0.0f;
    float     _targetPwmPct     = 0.0f;
    float     _activeRampDown   = RAMP_RATE_DOWN;

    // ─── Sinais elétricos sensorless — amostrados a ~100Hz, sempre vivos ────────
    float _vBusRawFilt = 0.0f;   // V, pós-EMA, pré-escala
    float _vMotRawFilt = 0.0f;
    float _vCurRawFilt = 0.0f;
    float _vBus = 0.0f;          // V, real (V_bus_filt * K_V_BUS)
    float _vMot = 0.0f;          // V, real (V_mot_filt * K_V_MOT)
    float _iA   = 0.0f;          // A, real, nunca negativo
    float _fcem = 0.0f;          // V, FCEM estimada (V_mot - I_a * MOTOR_R_A)

    // ─── Ganhos de tuning (cascata) + Ke sensorless ──────────────────────────────
    float _spdKp = PID_SPD_KP;
    float _spdKi = PID_SPD_KI;
    float _spdKd = PID_SPD_KD;
    float _curKp = PID_CUR_KP;
    float _curKi = PID_CUR_KI;
    float _curKd = PID_CUR_KD;
    float _fcemMaxV = FCEM_MAX_V_DEFAULT;

    // Aplicação de SetTunings() deferida para dentro de update() (task loop()) —
    // os setters acima são chamados pela task assíncrona do WS/BLE; evita
    // escrita cross-task nos internals de 64 bits (double) do PID_v1.
    bool _spdTuningDirty = false;
    bool _curTuningDirty = false;

    Preferences _prefs;

    unsigned long _lastUpdateMs        = 0;
    unsigned long _lastAdcSampleMs     = 0;   // cadência própria da amostragem ADC/EMA (~10ms)
    unsigned long _lastHeartbeatMs     = 0;   // último heartbeat de qualquer interface
    unsigned long _overCurrentSinceMs    = 0; // 0 = não está acima do limite rápido no momento
    unsigned long _sustainedCurrentSinceMs = 0; // 0 = não está acima do limite sustentado no momento
    unsigned long _overspeedAbsSinceMs   = 0; // 0 = não está acima do teto absoluto no momento
    unsigned long _overspeedTargetSinceMs = 0; // 0 = não está fugindo do alvo no momento

    void  applyPwmPct(float pct);
    float speedToPwmPct(float kmh) const;
    float _compensateBusVoltage(float pwmPct) const;   // feedforward — compensa queda de V_bus sob carga
    void  _loadTuningFromNvs();
    void  _sampleElectricalSignals();
    void  _checkOvercurrent();
    void  _checkOverspeed();
};

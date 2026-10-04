#pragma once
#include <Arduino.h>
#include "Config.h"

// Buzzer ATIVO — tem oscilador próprio, não aceita frequência (tone()/
// noTone() ligavam/desligavam esse oscilador na frequência pedida em vez de
// simplesmente energizá-lo, produzindo um som quebrado/errático). Controle
// aqui é só liga/desliga via digitalWrite; sons diferenciados por duração e
// ritmo, não por tom. Polaridade em BUZZER_ACTIVE_LOW (Config.h).
class Buzzer {
public:
    explicit Buzzer(uint8_t pin);

    void begin();
    void update();   // chamar a cada loop — avança beep temporizado e/ou alarme

    void click();          // toque de botão — pulso curto
    void countdownTick();  // contagem regressiva (3-2-1) — pulso médio
    void go();              // início — pulso mais longo

    void startAlarm(bool emergency);  // liga o padrão repetitivo (falha ou emergência)
    void stopAlarm();
    bool isAlarming() const { return _alarmActive; }

private:
    uint8_t _pin;

    void _on();
    void _off();
    void _beep(unsigned long durationMs);

    bool          _beepActive     = false;
    unsigned long _beepStartMs    = 0;
    unsigned long _beepDurationMs = 0;

    bool          _alarmActive      = false;
    bool          _alarmEmergency   = false;
    unsigned long _alarmCycleStartMs = 0;
};

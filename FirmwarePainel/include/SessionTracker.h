#pragma once
#include <Arduino.h>

// Tempo decorrido e distância percorrida, calculados localmente no painel a
// partir da velocidade recebida via telemetria — a placa de potência não
// expõe esses dois valores hoje (decisão: calcular aqui em vez de reabrir o
// projeto EsteiraC3). Distância é uma estimativa por amostragem periódica
// (~5Hz, cadência da telemetria), não um hodômetro contínuo no motor.
class SessionTracker {
public:
    void start();    // nova sessão — zera tempo/distância, começa a contar
    void pause();    // congela tempo e distância (setpoint volta a 0 externamente)
    void resume();   // retoma a contagem de onde parou
    bool isActive() const { return _active; }
    bool isPaused()  const { return _paused; }

    // Chamar a cada pacote de telemetria recebido — integra distância usando
    // o dt real desde a última amostra (só enquanto ativo, não pausado, e o
    // motor realmente reportando running=true).
    void onTelemetry(float speedKmh, bool motorRunning);

    unsigned long getElapsedMs() const;
    void getElapsedMMSS(uint8_t& minutes, uint8_t& seconds) const;
    float getDistanceKm() const { return _distanceKm; }

private:
    bool _active = false;
    bool _paused = false;

    unsigned long _elapsedBaseMs = 0;   // acumulado antes do trecho corrente
    unsigned long _runStartMs    = 0;   // início do trecho corrente (pós start/resume)
    unsigned long _lastSampleMs  = 0;   // última amostra de telemetria (pra dt)

    float _distanceKm = 0.0f;
};

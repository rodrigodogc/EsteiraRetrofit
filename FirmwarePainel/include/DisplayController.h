#pragma once
#include <Arduino.h>
#include <TreadmillDisplay.h>
#include "Config.h"
#include "PowerBoardProtocol.h"

// Estado visual de alto nível do painel — usado tanto pela máquina de estados
// em main.cpp quanto internamente pelo DisplayController pra detectar
// transições (entrar num estado novo dispara efeitos "de uma vez só", como
// iniciar um scrollText, sem repetir a cada chamada de show*()).
enum class PanelState : uint8_t {
    BOOT,
    DISCOVERING,
    IDLE,
    COUNTDOWN,
    RUNNING,
    PAUSED,
    FAULT,
    EMERGENCY,
    DISCONNECTED,
    DIAGNOSTICS,
    EASTER_EGG,
};

// Encapsula o TreadmillDisplay + toda a lógica de apresentação: pisca-pisca
// de setpoint (velocidade/inclinação), filtro de mediana da velocidade real
// exibida, cenas de cada estado do painel, animação de boot.
class DisplayController {
public:
    DisplayController();

    void begin();
    void update();   // avança scroll/blink/filtro — chamar a cada loop()

    void playBootAnimation();   // bloqueia por ~2-3s, chamado uma vez no setup()

    void showDiscovering();

    // Parado, a "velocidade real" é sempre 0 (motor desligado) — por
    // segurança, mostra o ALVO (o que o Start vai usar) em vez de zerar. Sem
    // isso, alguém podia herdar um alvo alto de uma sessão anterior (ex:
    // 10km/h) sem ver isso no display, que mostrava só "0.0" enquanto parado.
    void showIdle(float targetSpeedKmh);

    void showCountdown(uint8_t secondsLeft);
    void showRunning(uint8_t minutes, uint8_t seconds, float distanceKm);
    void showPaused(uint8_t minutes, uint8_t seconds, float distanceKm);
    void showFault(uint8_t faultCode);
    void showEmergency();
    void showDisconnected();
    void showDiagnostics(float vBus, float vMot, float current);
    void showEasterEgg();

    // Setpoint de velocidade mudou — pisca o alvo por TARGET_FLASH_DURATION_MS,
    // depois volta ao valor normal (velocidade real filtrada) sozinho.
    void flashTargetSpeed(float kmh);

    // Modo de ajuste de inclinação (botão central) — enquanto ativo, o bloco
    // de inclinação mostra "I" + valor alvo em vez da distância normal.
    void setInclineAdjustMode(bool active, float inclineDeg);

    // Alimenta o filtro de mediana — chamar sempre que uma nova amostra de
    // velocidade real chegar via telemetria (não precisa ser síncrono com a
    // taxa de exibição, que é deliberadamente mais lenta).
    void pushSpeedSample(float kmh);

private:
    TreadmillDisplay _display;
    PanelState        _lastState = PanelState::BOOT;

    unsigned long _lastRenderMs = 0;

    bool          _blinkOn           = true;
    unsigned long _lastBlinkToggleMs = 0;

    bool          _speedFlashActive  = false;
    float         _speedFlashValue   = 0.0f;
    unsigned long _speedFlashStartMs = 0;

    bool  _inclineModeActive = false;
    float _inclineModeValue  = 0.0f;

    float   _speedSamples[SPEED_FILTER_WINDOW] = {0};
    uint8_t _speedSampleCount = 0;
    uint8_t _speedSampleIndex = 0;
    float   _filteredSpeed       = 0.0f;
    unsigned long _lastFilterUpdateMs = 0;

    bool _enteringState(PanelState s);   // true só na 1a chamada após mudar de estado
    void _renderSpeedBlock(float realSpeedFiltered);
    void _renderInclineBlock(float distanceKm);
    float _medianOfSamples() const;
};

#include "DisplayController.h"

DisplayController::DisplayController()
    : _display(DISPLAY_DIN_PIN, DISPLAY_CLK_PIN) {}

void DisplayController::begin() {
    _display.begin(DISPLAY_BRIGHTNESS);
    _display.clear();
}

bool DisplayController::_enteringState(PanelState s) {
    if (_lastState != s) {
        _lastState = s;
        return true;
    }
    return false;
}

void DisplayController::update() {
    _display.update();   // avança scrollText não bloqueante, se houver um ativo

    unsigned long now = millis();

    if (now - _lastBlinkToggleMs >= TARGET_BLINK_INTERVAL_MS) {
        _lastBlinkToggleMs = now;
        _blinkOn = !_blinkOn;
    }

    if (_speedFlashActive && (now - _speedFlashStartMs > TARGET_FLASH_DURATION_MS)) {
        _speedFlashActive = false;
    }

    // Velocidade real exibida — deliberadamente lenta (pedido do usuário: não
    // precisa atualizar rápido, várias amostras + mediana antes de mostrar).
    if (now - _lastFilterUpdateMs >= SPEED_DISPLAY_UPDATE_MS) {
        _lastFilterUpdateMs = now;
        if (_speedSampleCount > 0) {
            _filteredSpeed = _medianOfSamples();
        }
    }
}

void DisplayController::playBootAnimation() {
    _display.clear();
    for (uint8_t b = 0; b <= DISPLAY_BRIGHTNESS; b++) {
        _display.setBrightness(b);
        delay(60);
    }
    _display.scrollText("  ESTEIRA RETROFIT  ", 130, false);
    while (_display.isScrolling()) {
        _display.update();
    }
    delay(200);
    _display.clear();
}

void DisplayController::pushSpeedSample(float kmh) {
    _speedSamples[_speedSampleIndex] = kmh;
    _speedSampleIndex = (_speedSampleIndex + 1) % SPEED_FILTER_WINDOW;
    if (_speedSampleCount < SPEED_FILTER_WINDOW) _speedSampleCount++;
}

float DisplayController::_medianOfSamples() const {
    float sorted[SPEED_FILTER_WINDOW];
    uint8_t n = _speedSampleCount;
    for (uint8_t i = 0; i < n; i++) sorted[i] = _speedSamples[i];

    // Insertion sort — janela pequena (<=7 amostras), custo desprezível.
    for (uint8_t i = 1; i < n; i++) {
        float key = sorted[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    return sorted[n / 2];
}

void DisplayController::flashTargetSpeed(float kmh) {
    _speedFlashActive  = true;
    _speedFlashValue   = kmh;
    _speedFlashStartMs = millis();
    _blinkOn = true;
}

void DisplayController::setInclineAdjustMode(bool active, float inclineDeg) {
    _inclineModeActive = active;
    _inclineModeValue  = inclineDeg;
}

void DisplayController::_renderSpeedBlock(float realSpeedFiltered) {
    if (_speedFlashActive) {
        if (_blinkOn) _display.setSpeed(_speedFlashValue);
        else          _display.writeText("   ", TreadmillDisplay::BLOCK_SPEED, false);
    } else {
        _display.setSpeed(realSpeedFiltered);
    }
}

void DisplayController::_renderInclineBlock(float distanceKm) {
    if (_inclineModeActive) {
        // Pisca o valor alvo (em vez de um "I" fixo — confundia com "1") —
        // indica visualmente que o par +/- está controlando inclinação, não
        // velocidade (ver AdjustMode em main.cpp).
        if (_blinkOn) _display.setIncline(_inclineModeValue);
        else          _display.writeText("   ", TreadmillDisplay::BLOCK_INCLINE, false);
        return;
    }
    _display.setIncline(distanceKm);
}

void DisplayController::showDiscovering() {
    if (_enteringState(PanelState::DISCOVERING)) {
        _display.clear();
        _display.scrollText(" PROCURANDO PLACA ", 140, true);
    }
}

void DisplayController::showIdle(float targetSpeedKmh) {
    if (_enteringState(PanelState::IDLE)) {
        _display.stopScroll();
        _display.clear();
    }
    unsigned long now = millis();
    if (now - _lastRenderMs < 150) return;
    _lastRenderMs = now;

    // Segurança: parado, mostra o ALVO (o que o Start vai usar), não "0.0" —
    // ver comentário em DisplayController.h. Continua piscando por
    // TARGET_FLASH_DURATION_MS logo após um ajuste (_speedFlashActive), e
    // depois assenta mostrando o mesmo valor de forma fixa.
    _renderSpeedBlock(targetSpeedKmh);
    _display.setTime(0, 0);
    _renderInclineBlock(0.0f);
}

void DisplayController::showCountdown(uint8_t secondsLeft) {
    if (_enteringState(PanelState::COUNTDOWN)) {
        _display.stopScroll();
        _display.clear();
    }
    char buf[2] = { (char)('0' + secondsLeft), '\0' };
    _display.writeText(buf, 2, false);   // último dígito do bloco velocidade — "  3", "  2", "  1"
}

void DisplayController::showRunning(uint8_t minutes, uint8_t seconds, float distanceKm) {
    if (_enteringState(PanelState::RUNNING)) {
        _display.stopScroll();
    }
    unsigned long now = millis();
    if (now - _lastRenderMs < 150) return;
    _lastRenderMs = now;

    _renderSpeedBlock(_filteredSpeed);
    _display.setTime(minutes, seconds);
    _renderInclineBlock(distanceKm);
}

void DisplayController::showPaused(uint8_t minutes, uint8_t seconds, float distanceKm) {
    _enteringState(PanelState::PAUSED);
    unsigned long now = millis();
    if (now - _lastRenderMs < 150) return;
    _lastRenderMs = now;

    if (_speedFlashActive) {
        _renderSpeedBlock(_filteredSpeed);
    } else if (_blinkOn) {
        _display.writeText("PAU", TreadmillDisplay::BLOCK_SPEED, false);
    } else {
        _display.writeText("   ", TreadmillDisplay::BLOCK_SPEED, false);
    }
    _display.setTime(minutes, seconds);
    _renderInclineBlock(distanceKm);
}

void DisplayController::showFault(uint8_t faultCode) {
    if (_enteringState(PanelState::FAULT)) {
        _display.clear();
        char msg[48];
        snprintf(msg, sizeof(msg), " FALHA: %s   ", faultMessage(faultCode));
        _display.scrollText(msg, 130, true);
    }
}

void DisplayController::showEmergency() {
    if (_enteringState(PanelState::EMERGENCY)) {
        _display.clear();
        _display.scrollText(" PARADA DE EMERGENCIA - REINSIRA O CLIPE ", 110, true);
    }
}

void DisplayController::showDisconnected() {
    if (_enteringState(PanelState::DISCONNECTED)) {
        _display.clear();
        _display.scrollText(" SEM SINAL DA PLACA ", 140, true);
    }
}

void DisplayController::showDiagnostics(float vBus, float vMot, float current) {
    if (_enteringState(PanelState::DIAGNOSTICS)) {
        _display.clear();
        char msg[56];
        snprintf(msg, sizeof(msg), " VBUS %.0fV  VMOT %.0fV  I %.1fA   ", vBus, vMot, current);
        _display.scrollText(msg, 120, true);
    }
}

void DisplayController::showEasterEgg() {
    if (_enteringState(PanelState::EASTER_EGG)) {
        _display.clear();
        _display.scrollText("   RODRIGO SILVA ESTEVE AQUI :)   ", 130, true);
    }
}

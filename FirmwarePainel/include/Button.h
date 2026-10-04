#pragma once
#include <Arduino.h>
#include "Config.h"

// Botão com debounce e auto-repeat opcional (usado nos botões de ajuste, que
// precisam repetir ao segurar — M/P/Start/modo usam só wasPressed()).
// INPUT_PULLUP — pino em nível baixo = pressionado.
class Button {
public:
    explicit Button(uint8_t pin);

    void begin();
    void update();   // chamar a cada loop()

    // Consome o evento (true só na primeira leitura após o toque/repetição)
    bool wasPressed();
    bool isRepeating();

    bool isHeld() const { return _stableState; }

private:
    uint8_t _pin;

    bool _rawState    = false;
    bool _stableState = false;
    unsigned long _lastChangeMs = 0;

    bool _pressedFlag  = false;
    bool _repeatFlag   = false;
    bool _repeating    = false;
    unsigned long _pressStartMs = 0;
    unsigned long _lastRepeatMs = 0;
};

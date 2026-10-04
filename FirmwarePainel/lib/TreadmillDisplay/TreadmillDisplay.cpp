/**
 * @file TreadmillDisplay.cpp
 * @brief Implementação da biblioteca TreadmillDisplay.
 */

#include "TreadmillDisplay.h"
#include "TreadmillFont.h"

#include <string.h>
#include <stdio.h>

using namespace TreadmillFont;

// -----------------------------------------------------------------------
// Mapeamento posição lógica (esquerda -> direita, "um display só") -> grid
// físico real do VK1640.
//
// Ordem física confirmada no painel: VELOCIDADE, TEMPO, INCLINAÇÃO.
// Cada bloco tem sua ordem interna cruzada/invertida por causa da fiação do
// painel (descoberta por engenharia reversa). Os grids 7 e 11-15 não fazem
// parte de nenhum bloco conhecido pelo firmware original.
// -----------------------------------------------------------------------
const uint8_t TreadmillDisplay::_positionToGrid[TreadmillDisplay::DIGIT_COUNT] = {
    10, 9, 8,    // Bloco VELOCIDADE -> posições 0..2
    4, 5, 6, 3,  // Bloco TEMPO      -> posições 3..6 (ordem cruzada no hardware)
    2, 1, 0      // Bloco INCLINAÇÃO -> posições 7..9
};

TreadmillDisplay::TreadmillDisplay(uint8_t dinPin, uint8_t clkPin)
    : _module(dinPin, clkPin, GRID_COUNT),
      _scrollLength(0),
      _scrollFrame(0),
      _scrollTotalFrames(0),
      _scrollStepMs(300),
      _scrollLastMs(0),
      _scrollActive(false),
      _scrollRepeat(true) {
    _scrollBuffer[0] = '\0';
}

// -------------------------------------------------------------------------
// Ciclo de vida
// -------------------------------------------------------------------------

void TreadmillDisplay::begin(uint8_t brightness) {
    _module.setupDisplay(true, brightness);
    _module.clearDisplay();
}

void TreadmillDisplay::clear() {
    _module.clearDisplay();
}

void TreadmillDisplay::setBrightness(uint8_t brightness) {
    if (brightness > 7) brightness = 7;
    _module.setupDisplay(true, brightness);
}

// -------------------------------------------------------------------------
// Conversão de segmentos: lógico (padrão) -> hardware (fiação do painel)
// -------------------------------------------------------------------------

uint8_t TreadmillDisplay::_logicalToHardware(uint8_t logicalSegments) {
    uint8_t a  = (logicalSegments >> 0) & 0x01;
    uint8_t b  = (logicalSegments >> 1) & 0x01;
    uint8_t c  = (logicalSegments >> 2) & 0x01;
    uint8_t d  = (logicalSegments >> 3) & 0x01;
    uint8_t e  = (logicalSegments >> 4) & 0x01;
    uint8_t f  = (logicalSegments >> 5) & 0x01;
    uint8_t g  = (logicalSegments >> 6) & 0x01;
    uint8_t dp = (logicalSegments >> 7) & 0x01;

    return (uint8_t)(
        (a  << 0) |
        (e  << 1) |
        (d  << 2) |
        (dp << 3) |
        (c  << 4) |
        (g  << 5) |
        (f  << 6) |
        (b  << 7)
    );
}

uint8_t TreadmillDisplay::charToHardwareByte(char c, bool dot) {
    uint8_t logical = TreadmillFont::charToLogicalSegments(c);
    if (dot) logical |= SEG_DP;
    return _logicalToHardware(logical);
}

// -------------------------------------------------------------------------
// Métricas de alto nível
// -------------------------------------------------------------------------

void TreadmillDisplay::setSpeed(float speed) {
    if (speed > 99.9f) speed = 99.9f;
    if (speed < 0.0f) speed = 0.0f;

    int val = (int)(speed * 10.0f);
    int d1 = (val / 100) % 10;
    int d2 = (val / 10) % 10;
    int d3 = val % 10;

    bool blankLeading = (speed < 10.0f);
    writeCharAt(BLOCK_SPEED + 0, blankLeading ? ' ' : (char)('0' + d1));
    writeCharAt(BLOCK_SPEED + 1, (char)('0' + d2), true); // ponto decimal sempre aceso
    writeCharAt(BLOCK_SPEED + 2, (char)('0' + d3));
}

void TreadmillDisplay::setTime(uint8_t minutes, uint8_t seconds) {
    if (minutes > 99) minutes = 99;
    if (seconds > 59) seconds = 59;

    writeCharAt(BLOCK_TIME + 0, (char)('0' + (minutes / 10)));
    writeCharAt(BLOCK_TIME + 1, (char)('0' + (minutes % 10)), true); // ":" sempre aceso
    writeCharAt(BLOCK_TIME + 2, (char)('0' + (seconds / 10)));
    writeCharAt(BLOCK_TIME + 3, (char)('0' + (seconds % 10)));
}

void TreadmillDisplay::setIncline(float incline) {
    if (incline > 99.9f) incline = 99.9f;
    if (incline < 0.0f) incline = 0.0f;

    int val = (int)(incline * 10.0f);
    int d1 = (val / 100) % 10;
    int d2 = (val / 10) % 10;
    int d3 = val % 10;

    bool blankLeading = (incline < 10.0f);
    writeCharAt(BLOCK_INCLINE + 0, blankLeading ? ' ' : (char)('0' + d1));
    writeCharAt(BLOCK_INCLINE + 1, (char)('0' + d2), true);
    writeCharAt(BLOCK_INCLINE + 2, (char)('0' + d3));
}

// -------------------------------------------------------------------------
// Escrita de caractere / texto / número
// -------------------------------------------------------------------------

void TreadmillDisplay::writeCharAt(uint8_t position, char c, bool dot) {
    if (position >= DIGIT_COUNT) return;
    uint8_t grid = _positionToGrid[position];
    _module.setSegments(charToHardwareByte(c, dot), grid);
}

void TreadmillDisplay::writeText(const char* text, uint8_t startPosition, bool clearRest) {
    if (text == NULL) return;
    size_t len = strlen(text);

    for (uint8_t pos = 0; pos < DIGIT_COUNT; pos++) {
        if (pos >= startPosition && (size_t)(pos - startPosition) < len) {
            writeCharAt(pos, text[pos - startPosition]);
        } else if (clearRest) {
            writeCharAt(pos, ' ');
        }
    }
}

void TreadmillDisplay::writeNumber(long number, uint8_t startPosition, uint8_t width, bool clearRest) {
    char buffer[12];
    if (width > 0 && width < sizeof(buffer)) {
        snprintf(buffer, sizeof(buffer), "%*ld", (int)width, number);
    } else {
        snprintf(buffer, sizeof(buffer), "%ld", number);
    }
    writeText(buffer, startPosition, clearRest);
}

void TreadmillDisplay::writeFloat(float number, uint8_t decimals, uint8_t startPosition, uint8_t width, bool clearRest) {
    char buffer[16];
    // dtostrf é usado em vez de snprintf("%f", ...) porque nem todos os
    // núcleos AVR do Arduino habilitam suporte a ponto flutuante no printf.
    dtostrf(number, width, decimals, buffer);

    char* start = buffer;
    if (width == 0) {
        while (*start == ' ') start++; // remove preenchimento quando não solicitado
    }
    writeText(start, startPosition, clearRest);
}

// -------------------------------------------------------------------------
// Texto rolante (letreiro / marquee)
// -------------------------------------------------------------------------

void TreadmillDisplay::scrollText(const char* text, uint16_t stepMs, bool repeat) {
    if (text == NULL) text = "";

    strncpy(_scrollBuffer, text, SCROLL_BUFFER_SIZE - 1);
    _scrollBuffer[SCROLL_BUFFER_SIZE - 1] = '\0';
    _scrollLength = (uint16_t)strlen(_scrollBuffer);

    _scrollStepMs = stepMs;
    _scrollRepeat = repeat;
    _scrollFrame = 0;
    // Frames necessários para o texto entrar completamente pela direita e
    // sair completamente pela esquerda: comprimento do texto + largura do
    // display (ver dedução no README).
    _scrollTotalFrames = _scrollLength + DIGIT_COUNT;
    _scrollLastMs = millis();
    _scrollActive = true;

    _renderScrollFrame();
}

void TreadmillDisplay::stopScroll() {
    _scrollActive = false;
}

bool TreadmillDisplay::isScrolling() const {
    return _scrollActive;
}

void TreadmillDisplay::update() {
    if (!_scrollActive) return;

    uint32_t now = millis();
    if ((uint32_t)(now - _scrollLastMs) < _scrollStepMs) return;
    _scrollLastMs = now;

    _scrollFrame++;
    if (_scrollFrame >= _scrollTotalFrames) {
        if (_scrollRepeat) {
            _scrollFrame = 0;
        } else {
            _scrollActive = false;
            clear();
            return;
        }
    }

    _renderScrollFrame();
}

void TreadmillDisplay::_renderScrollFrame() {
    for (uint8_t pos = 0; pos < DIGIT_COUNT; pos++) {
        int16_t charIndex = (int16_t)_scrollFrame + (int16_t)pos - (int16_t)DIGIT_COUNT;
        char c = ' ';
        if (charIndex >= 0 && charIndex < (int16_t)_scrollLength) {
            c = _scrollBuffer[charIndex];
        }
        writeCharAt(pos, c);
    }
}

// -------------------------------------------------------------------------
// Acesso de baixo nível
// -------------------------------------------------------------------------

void TreadmillDisplay::setRawGrid(uint8_t grid, uint8_t data) {
    if (grid < GRID_COUNT) {
        _module.setSegments(data, grid);
    }
}

void TreadmillDisplay::setRawSegment(uint8_t grid, uint8_t data) {
    setRawGrid(grid, data);
}

/**
 * @file TreadmillDisplay.h
 * @brief Biblioteca para o painel de LED VK1640/TM1640 de esteiras
 *        ergométricas (retrofit) com fiação de segmentos fora do padrão.
 *
 * O hardware deste painel não segue a ordem "de fábrica" de grids/segmentos
 * do chip VK1640 (compatível com o protocolo TM1640): tanto a ordem física
 * dos dígitos quanto a posição de cada segmento dentro do byte enviado ao
 * chip foram remapeados manualmente por engenharia reversa. Esta biblioteca
 * encapsula totalmente esse mapeamento — o código de aplicação só precisa
 * lidar com valores "normais" (velocidade, tempo, inclinação, texto, número).
 *
 * Recursos:
 *  - API de alto nível compatível com o painel original (velocidade, tempo
 *    MM:SS, inclinação).
 *  - Escrita de caractere individual em qualquer posição.
 *  - Escrita de texto e números tratando todos os displays como uma única
 *    linha contínua.
 *  - Efeito de texto rolante (letreiro) não bloqueante, passando por todos
 *    os displays como se fossem um só.
 *  - Acesso "raw" a qualquer grid, para casos avançados (ícones/LEDs
 *    indicadores mapeados manualmente).
 *
 * Compatível com Arduino IDE (1.8+/2.x) e PlatformIO.
 *
 * Dependência: "TM16xx LEDs and Buttons" de maxint-rd
 *              https://github.com/maxint-rd/TM16xx
 */

#ifndef TREADMILL_DISPLAY_H
#define TREADMILL_DISPLAY_H

#include <Arduino.h>
#include <TM1640.h>

class TreadmillDisplay {
public:
    /** Número total de grids endereçáveis pelo chip VK1640/TM1640. */
    static const uint8_t GRID_COUNT = 16;

    /**
     * Número de posições de caractere (dígitos de 7 segmentos) mapeadas e
     * utilizáveis como se fosse "um único display" contínuo.
     *
     * IMPORTANTE: apenas os grids já mapeados pelo painel original (blocos de
     * velocidade, tempo e inclinação) são usados aqui. O grid 7 e os grids
     * 11-15 não fazem parte de nenhum bloco conhecido e ficam de fora — ajuste
     * _positionToGrid (em TreadmillDisplay.cpp) se o seu painel tiver mais
     * dígitos de 7 segmentos disponíveis.
     */
    static const uint8_t DIGIT_COUNT = 10;

    /**
     * Posição lógica (0..DIGIT_COUNT-1, da esquerda para a direita) onde cada
     * bloco começa, útil para escrever texto/número dentro de uma região
     * específica com writeText()/writeNumber()/writeCharAt().
     *
     * Ordem física confirmada no painel (esquerda -> direita): velocidade,
     * tempo, inclinação. Caso essa ordem mude em outro hardware, basta
     * reordenar os grupos dentro de _positionToGrid[] em TreadmillDisplay.cpp
     * (e ajustar os valores deste enum de acordo).
     */
    enum Block : uint8_t {
        BLOCK_SPEED   = 0, // posições 0..2 (3 dígitos)
        BLOCK_TIME    = 3, // posições 3..6 (4 dígitos, MM:SS)
        BLOCK_INCLINE = 7, // posições 7..9 (3 dígitos)
    };

    /**
     * @param dinPin Pino conectado ao DIN do display.
     * @param clkPin Pino conectado ao CLK do display.
     */
    TreadmillDisplay(uint8_t dinPin, uint8_t clkPin);

    // ---------------------------------------------------------------------
    // Ciclo de vida
    // ---------------------------------------------------------------------

    /** Inicializa a comunicação e liga o display. @param brightness 0..7 */
    void begin(uint8_t brightness = 4);

    /** Apaga todos os segmentos do display (não afeta uma rolagem ativa). */
    void clear();

    /** Ajusta o brilho do display em tempo real. @param brightness 0..7 */
    void setBrightness(uint8_t brightness);

    // ---------------------------------------------------------------------
    // Métricas de alto nível (compatível com o comportamento original)
    // ---------------------------------------------------------------------

    /** Exibe a velocidade no bloco correspondente. Limites: 0.0 a 99.9. */
    void setSpeed(float speed);

    /** Exibe o tempo (MM:SS) no bloco correspondente. */
    void setTime(uint8_t minutes, uint8_t seconds);

    /** Exibe a inclinação no bloco correspondente. Limites: 0.0 a 99.9. */
    void setIncline(float incline);

    // ---------------------------------------------------------------------
    // Escrita de caractere / texto / número
    // ---------------------------------------------------------------------

    /**
     * @brief Escreve um único caractere em uma posição lógica (0..DIGIT_COUNT-1).
     * @param position Posição lógica, da esquerda para a direita.
     * @param c Caractere ASCII (' ', '0'-'9', 'A'-'Z'/'a'-'z' e alguns
     *          símbolos — veja TreadmillFont.h). Use '`' para o símbolo de grau (°).
     * @param dot Se true, acende também o ponto decimal desta posição.
     */
    void writeCharAt(uint8_t position, char c, bool dot = false);

    /**
     * @brief Escreve uma string estática tratando todos os displays como um só.
     * @param text Texto a exibir (apenas caracteres suportados pela fonte).
     * @param startPosition Posição lógica inicial (0..DIGIT_COUNT-1).
     * @param clearRest Se true, preenche com espaços as posições fora do texto.
     */
    void writeText(const char* text, uint8_t startPosition = 0, bool clearRest = true);

    /**
     * @brief Escreve um número inteiro como texto.
     * @param width Largura mínima em caracteres (preenchida com espaços à
     *              esquerda); 0 para largura natural.
     */
    void writeNumber(long number, uint8_t startPosition = 0, uint8_t width = 0, bool clearRest = true);

    /**
     * @brief Escreve um número de ponto flutuante como texto.
     * @param decimals Casas decimais.
     * @param width Largura mínima em caracteres (0 para largura natural).
     */
    void writeFloat(float number, uint8_t decimals, uint8_t startPosition = 0, uint8_t width = 0, bool clearRest = true);

    // ---------------------------------------------------------------------
    // Texto rolante (letreiro/marquee) — efeito de "texto infinito" passando
    // por todos os displays como se fossem um só
    // ---------------------------------------------------------------------

    /**
     * @brief Inicia uma rolagem de texto não bloqueante (entra pela direita,
     *        sai pela esquerda).
     * @param text Texto a rolar (é copiado internamente; buffer máximo definido
     *             por TREADMILL_SCROLL_BUFFER_SIZE, 128 por padrão).
     * @param stepMs Intervalo em milissegundos entre cada passo da rolagem.
     * @param repeat Se true, a rolagem reinicia automaticamente ao terminar.
     */
    void scrollText(const char* text, uint16_t stepMs = 300, bool repeat = true);

    /** Interrompe a rolagem atual (não limpa o display automaticamente). */
    void stopScroll();

    /** @return true se uma rolagem estiver em andamento. */
    bool isScrolling() const;

    /**
     * @brief Avança a animação de rolagem, se houver uma ativa.
     *        Não bloqueante — deve ser chamada a cada iteração de loop().
     *        Sem rolagem ativa, é um no-op de baixo custo.
     */
    void update();

    // ---------------------------------------------------------------------
    // Acesso de baixo nível
    // ---------------------------------------------------------------------

    /** Escreve um byte de segmentos já codificado para o hardware direto em um grid (0..15). */
    void setRawGrid(uint8_t grid, uint8_t data);

    /** @deprecated Alias de setRawGrid(), mantido por compatibilidade. */
    void setRawSegment(uint8_t grid, uint8_t data);

    /**
     * @brief Converte um caractere para o byte de segmentos já codificado
     *        para este hardware (útil para uso avançado com setRawGrid()).
     */
    static uint8_t charToHardwareByte(char c, bool dot = false);

private:
    static const uint16_t SCROLL_BUFFER_SIZE = 128;

    TM1640 _module;

    static const uint8_t _positionToGrid[DIGIT_COUNT];

    // Estado da rolagem (não bloqueante, controlado via millis()).
    char _scrollBuffer[SCROLL_BUFFER_SIZE];
    uint16_t _scrollLength;
    uint16_t _scrollFrame;
    uint16_t _scrollTotalFrames;
    uint16_t _scrollStepMs;
    uint32_t _scrollLastMs;
    bool _scrollActive;
    bool _scrollRepeat;

    void _renderScrollFrame();
    static uint8_t _logicalToHardware(uint8_t logicalSegments);
};

#endif // TREADMILL_DISPLAY_H

/**
 * @file TreadmillFont.h
 * @brief Tabela de fontes (7 segmentos) e conversão para a codificação de
 *        hardware do painel VK1640/TM1640 desta esteira.
 *
 * Uso interno da biblioteca TreadmillDisplay — não é uma API pública.
 *
 * ---------------------------------------------------------------------------
 * Convenção LÓGICA de segmentos (usada em toda a biblioteca, antes de
 * remontar para o hardware):
 *
 *   bit0=a  bit1=b  bit2=c  bit3=d  bit4=e  bit5=f  bit6=g  bit7=dp
 *
 *        _a_
 *     f |   | b
 *        _g_
 *     e |   | c
 *        _d_   .dp
 *
 * ---------------------------------------------------------------------------
 * Mapeamento de HARDWARE (engenharia reversa):
 *
 * O painel original desta esteira foi religado de forma não padrão. A partir
 * da fonte de dígitos 0-9 obtida por tentativa e erro (ver histórico do
 * projeto), foi possível deduzir matematicamente a posição real de cada
 * segmento dentro do byte enviado ao VK1640/TM1640:
 *
 *   bit0=a  bit1=e  bit2=d  bit3=dp  bit4=c  bit5=g  bit6=f  bit7=b
 *
 * Essa remontagem acontece em um único lugar do código
 * (TreadmillDisplay::_logicalToHardware), então todo o resto da biblioteca —
 * inclusive esta tabela de fontes — só precisa lidar com a convenção lógica
 * "normal" acima.
 * ---------------------------------------------------------------------------
 */

#ifndef TREADMILL_FONT_H
#define TREADMILL_FONT_H

#include <Arduino.h>

namespace TreadmillFont {

// Máscaras de segmento individuais (convenção lógica).
static const uint8_t SEG_A  = 0x01;
static const uint8_t SEG_B  = 0x02;
static const uint8_t SEG_C  = 0x04;
static const uint8_t SEG_D  = 0x08;
static const uint8_t SEG_E  = 0x10;
static const uint8_t SEG_F  = 0x20;
static const uint8_t SEG_G  = 0x40;
static const uint8_t SEG_DP = 0x80;

// Primeiro caractere coberto pela tabela abaixo.
static const char FONT_FIRST_CHAR = ' '; // 0x20

/**
 * Tabela ASCII de ' ' (0x20) a 'Z' (0x5A), em segmentos lógicos.
 * Letras minúsculas são convertidas para maiúsculas antes da consulta.
 *
 * Em 7 segmentos, algumas letras não têm representação única/perfeita.
 * As seguintes são aproximações visuais aceitas (limitação conhecida e
 * inerente a qualquer display de 7 segmentos): K, M, O, V, W, X.
 */
const uint8_t FONT_TABLE[] PROGMEM = {
    /* 0x20 ' ' */ 0x00,
    /* 0x21 '!' */ 0x00,
    /* 0x22 '"' */ 0x00,
    /* 0x23 '#' */ 0x00,
    /* 0x24 '$' */ 0x00,
    /* 0x25 '%' */ 0x00,
    /* 0x26 '&' */ 0x00,
    /* 0x27 '\'' */ 0x00,
    /* 0x28 '(' */ 0x39,
    /* 0x29 ')' */ 0x0F,
    /* 0x2A '*' */ 0x00,
    /* 0x2B '+' */ 0x00,
    /* 0x2C ',' */ 0x00,
    /* 0x2D '-' */ 0x40,
    /* 0x2E '.' */ 0x80,
    /* 0x2F '/' */ 0x00,
    /* 0x30 '0' */ 0x3F,
    /* 0x31 '1' */ 0x06,
    /* 0x32 '2' */ 0x5B,
    /* 0x33 '3' */ 0x4F,
    /* 0x34 '4' */ 0x66,
    /* 0x35 '5' */ 0x6D,
    /* 0x36 '6' */ 0x7D,
    /* 0x37 '7' */ 0x07,
    /* 0x38 '8' */ 0x7F,
    /* 0x39 '9' */ 0x6F,
    /* 0x3A ':' */ 0x80,
    /* 0x3B ';' */ 0x00,
    /* 0x3C '<' */ 0x00,
    /* 0x3D '=' */ 0x48,
    /* 0x3E '>' */ 0x00,
    /* 0x3F '?' */ 0x53,
    /* 0x40 '@' */ 0x00,
    /* 0x41 'A' */ 0x77,
    /* 0x42 'B' */ 0x7C,
    /* 0x43 'C' */ 0x39,
    /* 0x44 'D' */ 0x5E,
    /* 0x45 'E' */ 0x79,
    /* 0x46 'F' */ 0x71,
    /* 0x47 'G' */ 0x3D,
    /* 0x48 'H' */ 0x76,
    /* 0x49 'I' */ 0x06,
    /* 0x4A 'J' */ 0x1E,
    /* 0x4B 'K' */ 0x76, // aproximação (igual a H)
    /* 0x4C 'L' */ 0x38,
    /* 0x4D 'M' */ 0x15, // aproximação
    /* 0x4E 'N' */ 0x54,
    /* 0x4F 'O' */ 0x3F, // igual ao dígito 0
    /* 0x50 'P' */ 0x73,
    /* 0x51 'Q' */ 0x67,
    /* 0x52 'R' */ 0x50,
    /* 0x53 'S' */ 0x6D, // igual ao dígito 5
    /* 0x54 'T' */ 0x78,
    /* 0x55 'U' */ 0x3E,
    /* 0x56 'V' */ 0x3E, // aproximação (igual a U)
    /* 0x57 'W' */ 0x2A, // aproximação
    /* 0x58 'X' */ 0x76, // aproximação (igual a H)
    /* 0x59 'Y' */ 0x6E,
    /* 0x5A 'Z' */ 0x5B, // igual ao dígito 2
};

const uint16_t FONT_TABLE_SIZE = sizeof(FONT_TABLE);

/**
 * @brief Converte um caractere ASCII em uma máscara de segmentos lógica.
 * @param c Caractere de entrada. Minúsculas são tratadas como maiúsculas.
 *          O caractere de crase '`' é tratado como atalho para o símbolo de
 *          grau (°), útil para exibir inclinação/temperatura.
 * @return Máscara de segmentos lógica (bit0=a ... bit6=g, bit7=dp).
 *         Caracteres não suportados retornam 0x00 (em branco).
 */
inline uint8_t charToLogicalSegments(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c == '`') {
        return SEG_A | SEG_B | SEG_F | SEG_G; // símbolo de grau (°)
    }
    uint16_t idx = (uint16_t)(c - FONT_FIRST_CHAR);
    if (c < FONT_FIRST_CHAR || idx >= FONT_TABLE_SIZE) {
        return 0x00;
    }
    return pgm_read_byte(&FONT_TABLE[idx]);
}

} // namespace TreadmillFont

#endif // TREADMILL_FONT_H

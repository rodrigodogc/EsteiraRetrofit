#pragma once

// ─── Display (TreadmillDisplay — VK1640/TM1640) ──────────────────────────────
#define DISPLAY_DIN_PIN         16
#define DISPLAY_CLK_PIN         17
// NÃO é um pino — nível de brilho (0-7) enviado por comando do próprio
// protocolo TM1640, através do mesmo DIN/CLK acima (ver TreadmillDisplay::
// begin()/setBrightness()). Sem fiação extra nenhuma.
#define DISPLAY_BRIGHTNESS      7

// ─── Botões de ajuste — INPUT_PULLUP, para GND ──────────────────────────────
// Botões físicos originais de velocidade trocados por botões de multimídia
// (voltar faixa / avançar faixa) sobrando no painel — mesmos pinos, só a
// fiação/pulsador foi trocada. Esse par +/- controla velocidade por padrão,
// ou inclinação enquanto MODE_TOGGLE_PIN alterna o modo (ver AdjustMode em
// main.cpp). Os botões dedicados de inclinação (INCLINE_UP_PIN/
// INCLINE_DOWN_PIN) tinham parado de responder por causa do curto de chassi
// na tomada (resolvido) — não eram um problema de hardware/fiação deles
// mesmos — e voltam a funcionar em paralelo, com aplicação direta.
#define ADJUST_UP_PIN           32
#define ADJUST_DOWN_PIN         33
#define MODE_TOGGLE_PIN         26   // botão central de multimídia, já soldado — alterna velocidade/inclinação
#define INCLINE_UP_PIN          27
#define INCLINE_DOWN_PIN        14

// ─── Botões livres — INPUT_PULLUP, para GND ──────────────────────────────────
// GPIO12 evitado de propósito (strapping de tensão do flash — perigoso com
// pull-up interno ativo no boot).
#define BTN_M_PIN               18   // "Monitor" — mostra diagnóstico elétrico por alguns segundos
#define BTN_P_PIN               19   // "Pausa" — congela velocidade sem resetar sessão
#define BTN_START_PIN           21   // Iniciar/parar/limpar falha, conforme estado

// ─── Emergência — nível, resistor de pull EXTERNO obrigatório ───────────────
// GPIO34 é entrada-só (sem pull interno disponível) — proposital, reforça que
// a biasagem tem que vir de resistor externo, não de configuração de firmware.
// Circuito fechado (ímã no lugar) = seguro; aberto (puxado OU fio rompido) =
// emergência — fail-safe também contra rompimento do próprio fio.
// EMERGENCY_SAFE_LEVEL: nível lido no pino quando SEGURO. Inverta aqui se a
// fiação real for oposta — precisa validar na bancada antes do uso real.
#define EMERGENCY_PIN            34
#define EMERGENCY_SAFE_LEVEL     LOW
// Debounce — exige leitura "insegura" contínua por esse tempo antes de
// declarar emergência de verdade. Não substitui o resistor de pull externo
// (isso continua obrigatório) — é uma camada extra contra ruído elétrico
// transitório (ex: inrush de partida do motor), que se mostrou real na
// bancada com o motor de verdade. Curto o suficiente pra não atrasar uma
// emergência genuína de forma perceptível.
#define EMERGENCY_DEBOUNCE_MS    30

// ─── Buzzer ATIVO — liga/desliga puro via digitalWrite, sem controle de tom ──
// (tem oscilador próprio; tone()/LEDC não se aplicam aqui — descoberto na
// bancada que o buzzer real é ativo, não passivo como assumido inicialmente).
// Pull-up encontrado no pino do driver sugere transistor ativo-baixo (liga
// com o pino em LOW) — inverta aqui se a fiação real for oposta.
#define BUZZER_PIN               4
#define BUZZER_ACTIVE_LOW        true

// ─── Botões — debounce e auto-repeat (velocidade/inclinação) ────────────────
// Causa raiz do problema de EMI/flakiness era o botão/fiação física (trocado
// por botões de multimídia — ver Config.h/ADJUST_UP_PIN), não o firmware.
// Com isso resolvido, volta a um debounce responsivo.
#define BUTTON_DEBOUNCE_MS        50
// Repetição ao segurar — só usada pelos botões de ajuste (velocidade/
// inclinação). Intervalo deliberadamente lento (não os 150ms de antes) por
// segurança: evita o alvo disparar rápido demais se o botão ficar preso.
#define BUTTON_REPEAT_DELAY_MS    600   // tempo segurado até começar a repetir
#define BUTTON_REPEAT_INTERVAL_MS 300   // intervalo entre repetições

// ─── Passos de ajuste — mesmos valores já usados na página web (app.js) ─────
#define SPEED_STEP_KMH           0.1f
// Abaixo disso a malha fechada sensorless fica instável (feedback de FCEM
// pouco confiável em baixa velocidade — ver MIN_SPEED_KMH no EsteiraC3, mesmo
// valor espelhado aqui). O painel nunca deixa o alvo cair abaixo disso, e já
// inicia nesse piso por padrão (targetSpeedKmh em main.cpp).
#define SPEED_MIN_KMH            2.5f
#define SPEED_MAX_KMH            12.0f  // espelha MAX_SPEED_KMH do EsteiraC3
#define INCLINE_STEP_DEG         1.0f
#define INCLINE_MAX_DEG          12.0f  // espelha INCLINE_MAX_DEG do EsteiraC3

// ─── Exibição — pisca alvo, depois volta ao valor real ───────────────────────
#define TARGET_FLASH_DURATION_MS  2500  // por quanto tempo pisca o alvo após ajuste
#define TARGET_BLINK_INTERVAL_MS  300   // intervalo do pisca-pisca (mais rápido — 400ms ficava apagado tempo demais)

// ─── Modo de ajuste (velocidade/inclinação) — botão central (MODE_TOGGLE_PIN) ──
// Alterna qual grandeza o par +/- controla. Volta sozinho pro modo velocidade
// (padrão) depois desse tempo sem uso, pra não "esquecer" o painel em modo
// inclinação e confundir o operador.
#define ADJUST_MODE_TIMEOUT_MS    4000

// ─── Filtro de velocidade real exibida (mediana) ─────────────────────────────
#define SPEED_FILTER_WINDOW       7     // amostras (~5Hz de telemetria → ~1,4s de janela)
#define SPEED_DISPLAY_UPDATE_MS   1000  // não precisa atualizar rápido — pedido do usuário

// ─── Modo diagnóstico (botão M) ───────────────────────────────────────────────
#define DIAG_MODE_DURATION_MS     4000

// ─── Easter egg (botão P, só parado) ─────────────────────────────────────────
#define EASTER_EGG_DURATION_MS    6000

// ─── Contagem regressiva de início ────────────────────────────────────────────
#define COUNTDOWN_STEPS           3
#define COUNTDOWN_STEP_MS         1000

// ─── ESP-NOW — descoberta/pareamento ─────────────────────────────────────────
#define ESPNOW_CHANNEL_MIN        1
#define ESPNOW_CHANNEL_MAX        13
#define ESPNOW_CHANNEL_DWELL_MS   350   // tempo ouvindo em cada canal durante a varredura
#define ESPNOW_HEARTBEAT_INTERVAL_MS 1000
#define ESPNOW_PEER_TIMEOUT_MS    3000  // sem telemetria por esse tempo → volta a procurar (mesmo valor do watchdog da placa)
#define ESPNOW_EMERGENCY_RESEND_MS 300  // reenvio de emergencyStop enquanto o nível estiver ativo (ESP-NOW não garante entrega)

// ─── LED de status — indica conexão ESP-NOW com a placa de potência ─────────
// ESP32 WROOM DevKit: LED onboard em GPIO2 (D2), lógica normal (HIGH = aceso).
#define LED_PIN                  2
#define LED_ACTIVE_LOW           false
// Conectado (telemetria fresca): 3 piscadas rápidas a cada LED_CYCLE_MS.
// Desconectado: pisca lento contínuo. Mesmo padrão usado na placa de potência (EsteiraC3).
#define LED_BLINK_COUNT           3
#define LED_BLINK_ON_MS           80
#define LED_BLINK_OFF_MS          80
#define LED_CYCLE_MS              2000
#define LED_SLOW_ON_MS            500
#define LED_SLOW_OFF_MS           500

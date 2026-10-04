#include <Arduino.h>
#include "Config.h"
#include "PowerBoardProtocol.h"
#include "Button.h"
#include "Buzzer.h"
#include "SessionTracker.h"
#include "EspNowClient.h"
#include "DisplayController.h"
#include "StatusLed.h"

// ─── Instâncias globais ───────────────────────────────────────────────────────
Button adjustUpBtn(ADJUST_UP_PIN);     // botão de multimídia "avançar" — +
Button adjustDownBtn(ADJUST_DOWN_PIN); // botão de multimídia "voltar"   — -
Button modeBtn(MODE_TOGGLE_PIN);       // botão central — alterna velocidade/inclinação
Button inclineUpBtn(INCLINE_UP_PIN);     // dedicado — sempre inclinação, direto
Button inclineDownBtn(INCLINE_DOWN_PIN); // dedicado — sempre inclinação, direto
Button btnM(BTN_M_PIN);
Button btnP(BTN_P_PIN);
Button btnStart(BTN_START_PIN);

Buzzer            buzzer(BUZZER_PIN);
SessionTracker    session;
EspNowClient      espNow;
DisplayController display;
StatusLed         statusLed(LED_PIN, LED_ACTIVE_LOW);

// ─── Estado do painel ───────────────────────────────────────────────────────
PanelState panelState = PanelState::BOOT;

// O par adjustUpBtn/adjustDownBtn controla velocidade ou inclinação conforme
// AdjustMode — só um par físico disponível pros dois ajustes (ver Config.h).
enum class AdjustMode : uint8_t { SPEED, INCLINE };
AdjustMode    adjustMode        = AdjustMode::SPEED;
unsigned long adjustModeSinceMs = 0;   // entrada/última atividade em modo INCLINE — pro auto-retorno

// Já inicia no piso mínimo de velocidade (controle sensorless instável abaixo
// disso — ver SPEED_MIN_KMH) — a esteira deve partir nessa velocidade mesmo
// se ninguém tocar em nada antes de dar Start.
float targetSpeedKmh   = SPEED_MIN_KMH;
float targetInclineDeg = 0.0f;

uint8_t       countdownValue       = 0;
unsigned long countdownLastTickMs  = 0;

bool          diagModeActive  = false;
unsigned long diagModeStartMs = 0;

bool          easterEggActive  = false;
unsigned long easterEggStartMs = 0;

unsigned long lastEmergencyResendMs   = 0;
unsigned long lastEspNowStatusUpdateMs = 0;   // detecta chegada de telemetria nova

unsigned long emergencyUnsafeSinceMs = 0;   // 0 = não está lendo inseguro no momento

// Debounce — só declara emergência de verdade após EMERGENCY_DEBOUNCE_MS de
// leitura "insegura" contínua (ver comentário em Config.h). Um único pulso de
// ruído (ex: inrush de partida do motor) não passa disso; um clipe realmente
// puxado, sim.
bool isEmergencyTriggered() {
    bool unsafe = (digitalRead(EMERGENCY_PIN) != EMERGENCY_SAFE_LEVEL);
    unsigned long now = millis();

    if (!unsafe) {
        emergencyUnsafeSinceMs = 0;
        return false;
    }
    if (emergencyUnsafeSinceMs == 0) {
        emergencyUnsafeSinceMs = now;
    }
    return (now - emergencyUnsafeSinceMs) >= EMERGENCY_DEBOUNCE_MS;
}

void discardPendingButtonPresses() {
    // Descarta toques que ficaram "pendurados" durante um estado que ignorava
    // botões (ex: emergência) — evita disparo fantasma ao sair do estado.
    adjustUpBtn.wasPressed();    adjustUpBtn.isRepeating();
    adjustDownBtn.wasPressed();  adjustDownBtn.isRepeating();
    modeBtn.wasPressed();
    inclineUpBtn.wasPressed();   inclineUpBtn.isRepeating();
    inclineDownBtn.wasPressed(); inclineDownBtn.isRepeating();
    btnM.wasPressed();
    btnP.wasPressed();
    btnStart.wasPressed();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== Painel Superior — Esteira Retrofit ===");

    // Entrada de emergência — resistor de pull EXTERNO (ver Config.h), sem
    // INPUT_PULLUP/PULLDOWN interno de propósito.
    pinMode(EMERGENCY_PIN, INPUT);

    adjustUpBtn.begin();
    adjustDownBtn.begin();
    modeBtn.begin();
    inclineUpBtn.begin();
    inclineDownBtn.begin();
    btnM.begin();
    btnP.begin();
    btnStart.begin();

    buzzer.begin();
    statusLed.begin();

    display.begin();
    display.playBootAnimation();

    espNow.begin();
    panelState = PanelState::DISCOVERING;

    Serial.println("[Main] Setup completo");
}

void loop() {
    adjustUpBtn.update();
    adjustDownBtn.update();
    modeBtn.update();
    inclineUpBtn.update();
    inclineDownBtn.update();
    btnM.update();
    btnP.update();
    btnStart.update();
    buzzer.update();
    display.update();
    espNow.update();
    statusLed.update(espNow.isTelemetryFresh());

    // ── Emergência — prioridade máxima, interrompe qualquer outro estado ───
    if (isEmergencyTriggered()) {
        if (panelState != PanelState::EMERGENCY) {
            panelState = PanelState::EMERGENCY;
            buzzer.startAlarm(true);
            lastEmergencyResendMs = 0;   // força reenvio imediato
        }
        if (millis() - lastEmergencyResendMs >= ESPNOW_EMERGENCY_RESEND_MS) {
            lastEmergencyResendMs = millis();
            espNow.sendEmergencyStop();   // sem efeito se ainda não pareado — _sendCommand() ignora
        }
        display.showEmergency();
        return;   // nada mais é processado enquanto o clipe estiver puxado
    } else if (panelState == PanelState::EMERGENCY) {
        // Clipe reinserido — volta ao normal, mas NUNCA reinicia o motor sozinho
        buzzer.stopAlarm();
        discardPendingButtonPresses();
        panelState = espNow.isPaired() ? PanelState::IDLE : PanelState::DISCOVERING;
        return;
    }

    // ── Sem pareamento — procurando a placa de potência ─────────────────────
    if (!espNow.isPaired()) {
        panelState = PanelState::DISCOVERING;
        display.showDiscovering();
        return;
    }
    if (panelState == PanelState::DISCOVERING) {
        panelState = PanelState::IDLE;
    }

    // Cobre a janela entre a telemetria expirar e EspNowClient voltar sozinho
    // pra descoberta no próximo update().
    if (!espNow.isTelemetryFresh()) {
        display.showDisconnected();
        return;
    }

    const PowerBoardStatus& status = espNow.status();

    // ── Falha reportada pela placa ───────────────────────────────────────────
    if (status.fault != 0) {
        if (panelState != PanelState::FAULT) {
            panelState = PanelState::FAULT;
            buzzer.startAlarm(false);
        }
        display.showFault(status.fault);
        if (btnStart.wasPressed()) {
            buzzer.click();
            espNow.sendClearFault();
        }
        return;
    } else if (panelState == PanelState::FAULT) {
        buzzer.stopAlarm();
        panelState = PanelState::IDLE;
    }

    // ── Nova amostra de telemetria — alimenta filtro de velocidade e sessão ──
    if (status.lastUpdateMs != lastEspNowStatusUpdateMs) {
        lastEspNowStatusUpdateMs = status.lastUpdateMs;
        display.pushSpeedSample(status.speed);
        session.onTelemetry(status.speed, status.running);

        // Ressincroniza o alvo local com o que a placa realmente tem — só em
        // IDLE (nenhuma sessão em andamento pelo painel). Cobre o caso de a
        // página web ter mudado o alvo por fora do painel (ex: uso misto
        // web+painel) deixando targetSpeedKmh/targetInclineDeg do painel
        // desatualizados. Durante RUNNING/PAUSED o painel já é a única fonte
        // de comando de movimento (a placa recusa comandos da web nesse caso
        // — ver WebSocketHandler::panelHasPriority() no EsteiraC3), então o
        // valor local permanece autoritativo e não é sobrescrito aqui.
        if (panelState == PanelState::IDLE) {
            // max() com o piso — a placa reporta 0 antes da 1a sessão (nunca
            // deu start ainda), e o painel nunca deve oferecer um alvo abaixo
            // do mínimo operável (ver SPEED_MIN_KMH).
            targetSpeedKmh   = max(status.targetSpeed, SPEED_MIN_KMH);
            targetInclineDeg = status.targetIncline;
        }
    }

    // ── Botão M — modo diagnóstico (sobrepõe qualquer tela por alguns segundos) ──
    if (btnM.wasPressed() && panelState != PanelState::COUNTDOWN) {
        buzzer.click();
        diagModeActive  = true;
        diagModeStartMs = millis();
    }
    if (diagModeActive) {
        if (millis() - diagModeStartMs > DIAG_MODE_DURATION_MS) {
            diagModeActive = false;
        } else {
            display.showDiagnostics(status.vBus, status.vMot, status.current);
            return;
        }
    }

    // ── Ajuste de velocidade/inclinação — não durante a contagem regressiva ──
    // Só um par físico de botões (+/-) disponível — controla velocidade por
    // padrão, ou inclinação enquanto o modo estiver alternado pelo botão
    // central. Toque simples + repetição ao segurar (ver BUTTON_REPEAT_*).
    if (panelState != PanelState::COUNTDOWN) {
        if (modeBtn.wasPressed()) {
            adjustMode = (adjustMode == AdjustMode::SPEED) ? AdjustMode::INCLINE : AdjustMode::SPEED;
            adjustModeSinceMs = millis();
            buzzer.click();
        }

        // Modo inclinação volta sozinho pro modo velocidade (padrão) depois de
        // um tempo sem uso — evita "esquecer" o painel em modo inclinação.
        if (adjustMode == AdjustMode::INCLINE &&
            millis() - adjustModeSinceMs > ADJUST_MODE_TIMEOUT_MS) {
            adjustMode = AdjustMode::SPEED;
        }

        if (adjustMode == AdjustMode::SPEED) {
            bool speedChanged = false;
            if (adjustUpBtn.wasPressed() || adjustUpBtn.isRepeating()) {
                targetSpeedKmh = min(targetSpeedKmh + SPEED_STEP_KMH, SPEED_MAX_KMH);
                speedChanged = true;
            }
            if (adjustDownBtn.wasPressed() || adjustDownBtn.isRepeating()) {
                targetSpeedKmh = max(targetSpeedKmh - SPEED_STEP_KMH, SPEED_MIN_KMH);
                speedChanged = true;
            }
            if (speedChanged) {
                buzzer.click();
                espNow.sendSetSpeed(targetSpeedKmh);
                display.flashTargetSpeed(targetSpeedKmh);
            }
        } else {
            bool inclineChanged = false;
            if (adjustUpBtn.wasPressed() || adjustUpBtn.isRepeating()) {
                targetInclineDeg = min(targetInclineDeg + INCLINE_STEP_DEG, INCLINE_MAX_DEG);
                inclineChanged = true;
            }
            if (adjustDownBtn.wasPressed() || adjustDownBtn.isRepeating()) {
                targetInclineDeg = max(targetInclineDeg - INCLINE_STEP_DEG, 0.0f);
                inclineChanged = true;
            }
            if (inclineChanged) {
                adjustModeSinceMs = millis();   // renova o timeout do modo a cada ajuste
                buzzer.click();
                espNow.sendSetIncline(targetInclineDeg);
            }
        }

        display.setInclineAdjustMode(adjustMode == AdjustMode::INCLINE, targetInclineDeg);

        // ── Botões dedicados de inclinação — controle direto, sempre ativo,
        // em paralelo ao par compartilhado acima (independem de adjustMode).
        bool inclineDirectChanged = false;
        if (inclineUpBtn.wasPressed() || inclineUpBtn.isRepeating()) {
            targetInclineDeg = min(targetInclineDeg + INCLINE_STEP_DEG, INCLINE_MAX_DEG);
            inclineDirectChanged = true;
        }
        if (inclineDownBtn.wasPressed() || inclineDownBtn.isRepeating()) {
            targetInclineDeg = max(targetInclineDeg - INCLINE_STEP_DEG, 0.0f);
            inclineDirectChanged = true;
        }
        if (inclineDirectChanged) {
            buzzer.click();
            espNow.sendSetIncline(targetInclineDeg);
        }
    }

    // ── Botão P — pausa/retoma (rodando/pausado) ou easter egg (parado) ─────
    if (btnP.wasPressed()) {
        if (panelState == PanelState::RUNNING || panelState == PanelState::PAUSED) {
            buzzer.click();
            if (panelState == PanelState::RUNNING) {
                session.pause();
                espNow.sendSetSpeed(0.0f);   // congela sem "stop" — placa continua running=true
                panelState = PanelState::PAUSED;
            } else {
                session.resume();
                espNow.sendSetSpeed(targetSpeedKmh);
                panelState = PanelState::RUNNING;
            }
        } else if (panelState == PanelState::IDLE) {
            buzzer.click();
            easterEggActive  = true;
            easterEggStartMs = millis();
        }
    }

    if (easterEggActive) {
        if (millis() - easterEggStartMs > EASTER_EGG_DURATION_MS) {
            easterEggActive = false;
        } else {
            display.showEasterEgg();
            return;
        }
    }

    // ── Botão Start — inicia (com contagem) / para, conforme o estado ────────
    if (btnStart.wasPressed() && panelState != PanelState::COUNTDOWN) {
        buzzer.click();
        if (panelState == PanelState::IDLE) {
            panelState          = PanelState::COUNTDOWN;
            countdownValue       = COUNTDOWN_STEPS;
            countdownLastTickMs  = millis();
            buzzer.countdownTick();
        } else if (panelState == PanelState::RUNNING || panelState == PanelState::PAUSED) {
            espNow.sendStop();
            panelState = PanelState::IDLE;
        }
    }

    // ── Renderização conforme o estado ────────────────────────────────────────
    switch (panelState) {
        case PanelState::IDLE:
            display.showIdle(targetSpeedKmh);
            break;

        case PanelState::COUNTDOWN: {
            display.showCountdown(countdownValue);
            unsigned long now = millis();
            if (now - countdownLastTickMs >= COUNTDOWN_STEP_MS) {
                countdownLastTickMs = now;
                countdownValue--;
                if (countdownValue == 0) {
                    espNow.sendSetSpeed(targetSpeedKmh);
                    espNow.sendStart();
                    session.start();
                    buzzer.go();
                    panelState = PanelState::RUNNING;
                } else {
                    buzzer.countdownTick();
                }
            }
            break;
        }

        case PanelState::RUNNING: {
            uint8_t m, s;
            session.getElapsedMMSS(m, s);
            display.showRunning(m, s, session.getDistanceKm());
            break;
        }

        case PanelState::PAUSED: {
            uint8_t m, s;
            session.getElapsedMMSS(m, s);
            display.showPaused(m, s, session.getDistanceKm());
            break;
        }

        default:
            break;
    }
}

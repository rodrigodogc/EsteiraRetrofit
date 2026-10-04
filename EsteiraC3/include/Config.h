#pragma once

// ─── Controle de malha ────────────────────────────────────────────────────────
// true  = malha aberta (target speed → PWM linear, sem sensor)
// false = malha fechada (cascata PID sensorless — FCEM estimada via ADC)
// Malha fechada validada em bancada (MOTOR_R_A=1.9Ω, PID_SPD_KP=0.5) — deve
// ser o modo de partida padrão. setMode ainda permite trocar em runtime para
// testes pontuais (motor precisa estar parado), mas o boot não deve depender
// disso — antes o padrão ficou em "true" (aberta) e nunca foi revertido após
// a validação, então todo boot partia em malha aberta silenciosamente.
#define OPEN_LOOP_MODE         false

// ─── PWM do Motor ─────────────────────────────────────────────────────────────
// GPIO21 — só fica livre porque ARDUINO_USB_CDC_ON_BOOT=1 (platformio.ini) move
// o Serial para o USB nativo; sem esse flag, 21 é o TX do UART0. Uso temporário
// para testes de bancada — avaliar mover para GPIO4 (sem função especial) antes
// de fiação definitiva, caso queira manter Serial em UART0 no futuro.
#define MOTOR_PWM_PIN          20
#define MOTOR_PWM_CHANNEL      0
#define MOTOR_PWM_FREQ_HZ      18000     // 20 kHz (audível-livre para MOSFETs)
#define MOTOR_PWM_RESOLUTION   10        // 10 bits → 0-1023
#define MOTOR_PWM_MAX_COUNTS   ((1 << MOTOR_PWM_RESOLUTION) - 1)  // 1023

// Limite de duty cycle em percentual — limite absoluto de segurança
#define MAX_PWM_PERCENT        45.0f
#define MAX_PWM_COUNTS         ((uint32_t)(MOTOR_PWM_MAX_COUNTS * MAX_PWM_PERCENT / 100.0f))

// ─── Velocidade ───────────────────────────────────────────────────────────────
#define MAX_SPEED_KMH          12.0f     // km/h — velocidade máxima permitida
// Abaixo disso o controle sensorless fica instável — o feedback de FCEM
// (subtração de duas leituras ruidosas) perde confiabilidade em baixa
// velocidade, mesmo acima da zona cega de handoff (FCEM_BLIND_ZONE_KMH).
// Validado na bancada: 2.5 km/h é o piso onde a malha ainda rastreia bem.
#define MIN_SPEED_KMH          2.5f      // km/h — velocidade mínima ao ligar

// Metros percorridos pela lona por pulso do encoder.
// Medido diretamente: lona avança 5 cm por volta do motor → 0,05 m ÷ 36 furos
#define ENCODER_PULSES_PER_REV 36        // furos no disco óptico
#define METERS_PER_PULSE       0.001389f // 0,05 m/rev ÷ 36 pulsos/rev

// ─── Rampa de Aceleração / Desaceleração ──────────────────────────────────────
// Unidade: % de duty-cycle por segundo
#define RAMP_RATE_UP           2.5f      // aceleração (subida)
#define RAMP_RATE_DOWN         5.0f      // desaceleração normal
#define RAMP_RATE_EMERGENCY    40.0f     // desaceleração de emergência

// ─── Sensorless FCEM — ADC (ESP32-C3, ADC1 — MCU original da placa de potência
// foi removido fisicamente, sem mais pin contention/tensão fantasma) ─────────
// PINO_VBUS: tensão do barramento retificado
// PINO_VMOT: tensão no terminal M+ do motor (saída flutuante do MOSFET high-side)
// PINO_CURR: corrente da armadura via shunt + ampop (já tratado, leitura precisa)
#define PINO_VBUS               0
#define PINO_VMOT                1
#define PINO_CURR                3

// Leitura via analogReadMilliVolts()/1000.0f (calibração nativa do ADC),
// filtrada por EMA (y = ALPHA*x + (1-ALPHA)*y_prev) a ~100Hz — cadência própria,
// desacoplada do gate de controle do PID (ver Motor::update()).
#define EMA_ALPHA                0.1f
#define ADC_SAMPLE_INTERVAL_MS   10

// ─── Constantes elétricas — calibradas em bancada, não alterar sem recalibração
#define K_V_BUS                  204.0f    // ganho do divisor do barramento
#define K_V_MOT                  102.0f    // ganho do divisor do motor
#define CONST_I                  0.1155f   // V/A — sensibilidade shunt+ampop
#define OFFSET_I                 0.041f    // V — piso térmico de repouso do ampop
// Medições discordantes: multímetro deu 2.5Ω e depois 1.9Ω (dependente da
// posição angular do rotor — normal em motor comutado, corrente quase zero);
// rotor travado em corrente real (35V/7A) deu 5.0Ω. Sem encoder disponível
// pra validar contra velocidade real, o critério que sobrou foi o
// comportamento em malha fechada: com 1.9Ω + PID_SPD_KP=0.5 a malha rastreia
// a referência corretamente na bancada — validado empiricamente, não é
// consenso teórico entre os métodos de medição (a discrepância com o rotor
// travado não está totalmente explicada). Reavaliar se o comportamento mudar.
#define MOTOR_R_A                1.9f      // ohms — resistência de armadura (validado empiricamente em malha fechada)

// FCEM esperada em MAX_SPEED_KMH — o valor abaixo NÃO foi recalculado para o
// MOTOR_R_A=1.9Ω atual (a conta pela placa do motor com R_a=1.9 daria
// FCEM_nominal=220-6.5*1.9=207.65V a 5400RPM → Ke=0.03845 V/RPM → a 4000RPM
// (MAX_SPEED_KMH=12km/h, ver METERS_PER_PULSE) → ~154V). Mantido em 139V
// porque é o que está validado empiricamente rodando junto com
// PID_SPD_KP=0.5 — não mexer sem reteste na bancada.
#define FCEM_MAX_V_DEFAULT       139.0f

// Abaixo desta velocidade rampada, a FCEM estimada (subtração de duas medições
// independentes, cada uma com seu próprio piso de ruído) não é confiável —
// usa-se feedforward puro até cruzar este limiar (handoff bumpless na cascata).
#define FCEM_BLIND_ZONE_KMH      1.5f

// ─── Compensação de queda de barramento (feedforward) ────────────────────────
// V_bus não é constante sob carga (cai com corrente, típico de retificação sem
// regulação ativa). O duty% de qualquer uma das 3 fontes (malha aberta, zona
// cega, ou saída do PID de corrente) é escalado por V_BUS_NOMINAL/V_bus_medido
// antes de aplicar no PWM — mantém a tensão média real entregue ao motor
// consistente independente da flutuação do barramento, tirando essa variação
// do que o PID precisa perseguir via integral.
// V_BUS_NOMINAL: ajustar para o V_bus real medido em repouso (aba Manutenção).
#define V_BUS_NOMINAL              300.0f
#define V_BUS_COMP_MIN_RATIO       0.6f     // limite inferior do fator (evita sub-compensar)
#define V_BUS_COMP_MAX_RATIO       1.6f     // limite superior do fator (evita disparo em leitura ruim)
#define V_BUS_MIN_FOR_COMPENSATION 20.0f    // abaixo disso, leitura é considerada inválida (bus ausente/ADC não assentou)

// Malha fechada: leitura inválida de V_bus não compensa (sintoma de sensor com
// problema não deve ser mascarado). Malha aberta: assume este valor padrão e
// segue compensando normalmente — modo de bring-up/bancada mais tolerante.
#define V_BUS_OPEN_LOOP_FALLBACK   300.0f

// ─── Proteção de corrente — dois patamares ───────────────────────────────────
#define MAX_ARMATURE_CURRENT_A     12.0f   // clamp de I_ref (saída da malha externa)
#define OVERCURRENT_TRIP_A         17.0f   // trip rápido — curto/falha
#define OVERCURRENT_TRIP_MS        150     // debounce do trip rápido
#define SUSTAINED_CURRENT_TRIP_A   9.5f    // trip lento — jam mecânico/sobrecarga térmica
#define SUSTAINED_CURRENT_TRIP_MS  5000    // debounce do trip lento

// ─── Proteção de sobrevelocidade — dois patamares, independente da malha ─────
// Velocidade usada é a estimada via FCEM (getSpeedFromFcemKmh) — não depende
// do encoder. Camada de segurança independente da qualidade do tuning da
// malha externa (mesmo princípio da proteção de sobrecorrente): existe
// mesmo que a malha esteja bem ajustada, não é substituto de bom tuning.
#define OVERSPEED_ABS_KMH            14.0f  // teto absoluto (MAX_SPEED_KMH+margem) — vale em qualquer modo
#define OVERSPEED_ABS_TRIP_MS        200    // debounce do trip rápido (teto absoluto)

#define OVERSPEED_TARGET_MARGIN_KMH  2.0f   // excesso tolerado acima do setpoint rampado — só malha fechada
#define OVERSPEED_TARGET_TRIP_MS     600    // debounce do trip por fuga do alvo (ex: descida da esteira)

// ─── PID em Cascata (malha dupla) — validados em bancada com o motor real ────
// Malha externa: velocidade/FCEM → corrente de referência (I_ref)
// Kp validado em bancada (subiu de 0.2 até rastrear a referência corretamente,
// junto com a correção de MOTOR_R_A para 1.9Ω). Ki não foi retunado nessa
// rodada — se sobrar erro de regime (velocidade não converge exatamente no
// alvo), é o próximo a ajustar.
#define PID_SPD_KP               0.5f      // A/V
#define PID_SPD_KI               0.1f      // A/(V·s)
#define PID_SPD_KD               0.0f
#define PID_SPD_INTERVAL_MS      100       // 10Hz — malha lenta

// Malha interna: corrente → duty-cycle do PWM
// Ganho estático da planta dI/dDuty% ≈ (V_bus/100)/R_a — com R_a corrigido
// para 5.0Ω (rotor travado), fica ~(300/100)/5.0 ≈ 0.6 A/% (era ~1.2 A/% com
// o R_a de 2.5Ω usado antes). Mantendo os valores reduzidos por ora — não há
// relato de problema na malha de corrente após a última redução; se ela
// parecer lenta demais, dá pra subir Kp_cur um pouco (o R_a maior sugere que
// a planta responde menos por % de duty do que se assumia).
#define PID_CUR_KP               0.4f      // %/A
#define PID_CUR_KI               4.0f      // %/(A·s)
#define PID_CUR_KD               0.0f
#define PID_CUR_INTERVAL_MS      10        // 100Hz — malha rápida

// ─── Encoder óptico (disco perfurado 36 furos, borda de subida) ──────────────
// Sensor de distância/reflexão focado nos furos; saída digital.
// GPIO5 — ESP32-C3 Super Mini: GPIO comum, com pull-up interno e suporte a
// interrupção; ainda assim use resistor externo 10kΩ→3V3 para imunidade a EMI.
//
// Limites de velocidade com os valores acima:
//   Máx suportada  : 1e6µs/ENCODER_DEBOUNCE_MIN_US / 36 * 60 ≈ 8333 RPM do motor
//   A 14 km/h      : ~2970 pulsos/s → período ~337 µs → debounce deve ser < 337 µs
#define ENCODER_PIN              5
#define ENCODER_DEBOUNCE_MIN_US  300     // µs — mínimo físico a 12 km/h ≈ 417 µs; margem anti-ruído
#define ENCODER_MAX_PERIOD_US    50000   // µs — acima disto reseta baseline (50 ms = ~0,1 km/h)
#define ENCODER_TIMEOUT_MS       1000    // ms sem pulso → velocidade = 0 (a 1 km/h: ~5 ms/pulso)
#define ENCODER_SPEED_ALPHA      0.35f   // coeficiente IIR: menor = mais suave mas mais lento

// ─── Inclinação ───────────────────────────────────────────────────────────────
#define INCLINE_UP_PIN         6         // relé UP (ativo HIGH)   — GPIO6 (Super Mini)
#define INCLINE_DOWN_PIN       7         // relé DOWN (ativo HIGH) — GPIO7 (Super Mini)
#define INCLINE_MAX_DEG        12.0f     // ângulo máximo em graus
#define INCLINE_MIN_DEG        0.0f      // ângulo mínimo (nível)
#define INCLINE_DEG_PER_SEC    1.2f      // graus por segundo de movimento
#define INCLINE_HOMING_MS      35000     // tempo máximo de homing (ms) — curso completo

// ─── WebSocket / Heartbeat ────────────────────────────────────────────────────
// Motor para IMEDIATAMENTE se não receber heartbeat dentro do timeout
#define WS_HEARTBEAT_TIMEOUT_MS  3000    // ms sem heartbeat → parar motor
#define WS_STATUS_INTERVAL_MS    200     // frequência de envio de status ao cliente

// ─── WiFi / Rede ──────────────────────────────────────────────────────────────
#define WIFI_AP_SSID           "Esteira-Config"
#define WIFI_AP_PASSWORD       "12345678"
#define WIFI_AP_IP             "192.168.4.1"
#define MDNS_HOSTNAME          "esteira"    // → http://esteira.local
#define WIFI_CONNECT_TIMEOUT_MS  15000
#define WIFI_RECONNECT_INTERVAL_MS 30000

// ─── NVS (Preferences) ───────────────────────────────────────────────────────
#define NVS_NAMESPACE          "esteira"
#define NVS_KEY_SSID           "ssid"
#define NVS_KEY_PASS           "pass"
#define NVS_KEY_PID_SPD_KP     "pid_spd_kp"
#define NVS_KEY_PID_SPD_KI     "pid_spd_ki"
#define NVS_KEY_PID_SPD_KD     "pid_spd_kd"
#define NVS_KEY_PID_CUR_KP     "pid_cur_kp"
#define NVS_KEY_PID_CUR_KI     "pid_cur_ki"
#define NVS_KEY_PID_CUR_KD     "pid_cur_kd"
#define NVS_KEY_FCEM_MAX_V     "fcem_max_v"

// ─── WiFi (1 = ligado, 0 = desliga WiFi/WebServer — ESP-NOW funciona nos dois
// casos, EspNowHandler liga o rádio STA por conta própria se necessário) ─────
#define WIFI_ENABLED           1

// ─── ESP-NOW (comunicação com a ESP32 do painel superior) ────────────────────
// Sem MAC fixo — pareamento é dinâmico (ver EspNowHandler::registerPeer()).
// Mesmos comandos de WSCommands.h; telemetria usa chaves compactas próprias
// (ver EspNowCommands.h) para caber no limite de payload do ESP-NOW (250 bytes,
// ESP_NOW_MAX_DATA_LEN, teto rígido do ESP32-C3/IDF4.4).
// Cadência do beacon broadcast (anuncia canal atual). Precisa ser MENOR que
// ESPNOW_CHANNEL_DWELL_MS do painel (350ms) — senão a varredura de canal pode
// levar várias voltas completas (13 canais x 350ms ≈ 4,5s cada) até por acaso
// coincidir o dwell no canal certo com um beacon, causando descoberta lenta e
// aleatória (~7-15s observado). Com o beacon mais rápido que o dwell, todo
// canal certo garante pegar pelo menos 1 beacon já na 1a passada.
#define ESPNOW_BEACON_INTERVAL_MS  150

// ─── LED de status — indica conexão ESP-NOW com o painel superior ───────────
// ESP32-C3 SuperMini: LED onboard em GPIO8, lógica INVERTIDA (LOW = aceso).
#define LED_PIN                  8
#define LED_ACTIVE_LOW           true
// Conectado (peer com heartbeat fresco): 3 piscadas rápidas a cada LED_CYCLE_MS.
// Desconectado: pisca lento contínuo. Mesmo padrão usado no painel (FirmwarePainel).
#define LED_BLINK_COUNT           3
#define LED_BLINK_ON_MS           80
#define LED_BLINK_OFF_MS          80
#define LED_CYCLE_MS              2000
#define LED_SLOW_ON_MS            500
#define LED_SLOW_OFF_MS           500

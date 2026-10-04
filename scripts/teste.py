import serial
import time
import sys

# Configurações
PORTA = 'COM8'
BAUD_RATE = 9600 # 9600 ou 115200 são os padrões para ISP
BYTE_SYNC = b'\x7F' # Byte clássico de Auto-Baud para 8051

try:
    ser = serial.Serial(PORTA, BAUD_RATE, timeout=0.1)
    print(f"[*] Porta {PORTA} aberta com sucesso.")
    print("[*] Iniciando bombardeamento de SYNC (0x7F)...")
    print("\n>>> ATENÇÃO: LIGUE O FIO VCC (5V) DA PLACA AGORA! <<<\n")
    
    start_time = time.time()
    
    while True:
        # Envia o byte mágico continuamente
        ser.write(BYTE_SYNC)
        
        # Ouve a resposta
        if ser.in_waiting > 0:
            resposta = ser.read(ser.in_waiting)
            print(f"\n[SUCESSO] O Megawin acordou e respondeu!")
            print(f"HEX Recebido: {' '.join(f'{b:02X}' for b in resposta)}")
            break
            
        # Se passar 15 segundos sem reposta, encerra
        if time.time() - start_time > 15:
            print("\n[FALHA] Timeout. Nenhuma resposta do chip.")
            print("Verifique se as conexões RX/TX não estão invertidas e tente novamente.")
            break
            
    ser.close()

except Exception as e:
    print(f"Erro ao acessar a porta serial: {e}")

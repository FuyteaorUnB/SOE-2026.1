import cv2
import os
import warnings
import numpy as np
import time
from skimage.feature import local_binary_pattern
from gpiozero import Servo, DigitalOutputDevice, RGBLED
from time import sleep

warnings.filterwarnings("ignore")

# PARÂMETROS DE SEGURANÇA E LIVENESS (ANTI-SPOOFING)
LBP_RAIO = 1
LBP_PONTOS = 8
LBP_METODO = 'uniform'
VARIANCIA_MIN = 8
QUADROS_NECESSARIOS = 3
TEMPO_RESET = 10         # Tempo em segundos para zerar o contador por inatividade

# Variáveis de controle de estado
contador_real = 0
ultimo_tempo_sucesso = 0 

# CONFIGURAÇÃO DE HARDWARE
led    = RGBLED(red=22, green=27, blue=10)
tranca = DigitalOutputDevice(17) 
servo  = Servo(18)               

# CONFIGURAÇÃO DE VISÃO
caminho_xml = 'haarcascade_frontalface_default.xml'
if not os.path.exists(caminho_xml):
    print(f"ERRO CRÍTICO: O arquivo {caminho_xml} não foi encontrado.")
    exit()

face_cascade = cv2.CascadeClassifier(caminho_xml)

cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 320)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)

def eh_rosto_real(gray, x, y, w, h):
    roi = gray[y:y+h, x:x+w]
    if roi.size == 0 or w < 60 or h < 60:
        return False
    
    # 1. Redimensionamento controlado para manter a textura dos poros
    roi = cv2.resize(roi, (120, 120))
    # 2. Filtro de Aguçamento (Sharpening)
    kernel = np.array([[-1,-1,-1], [-1,9,-1], [-1,-1,-1]])
    roi = cv2.filter2D(roi, -1, kernel)
    
    # 3. Cálculo da Variância LBP
    lbp = local_binary_pattern(roi, LBP_PONTOS, LBP_RAIO, method=LBP_METODO)
    variancia = np.var(lbp)
    
    resultado = variancia > VARIANCIA_MIN
    print(f"[LBP] Textura: {variancia:.2f} | Limite: {VARIANCIA_MIN} | {'REAL' if resultado else 'FOTO/TELA'}")
    
    return resultado

def liberar_acesso():
    global contador_real
    print("\n" + "="*40)
    print("ACESSO AUTORIZADO - ABRINDO TRAVA")
    print("="*40)
    
    led.color = (0, 1, 0) 
    tranca.on()           
    servo.max()           
    sleep(4)              
    
    print("FECHANDO TRAVA E REARMANDO SISTEMA")
    tranca.off()
    servo.min()
    led.color = (1, 0, 0) 
    contador_real = 0     # Zera após o sucesso
    sleep(2)              

# LOOP PRINCIPAL DE MONITORAMENTO
print("\n" + "#"*45)
print("SISTEMA DE SEGURANÇA INICIADO")
print(f"  ANTI-SPOOFING: Ativo | Alvos: {QUADROS_NECESSARIOS}")
print(f"  TIMEOUT DE MEMÓRIA: {TEMPO_RESET}s")
print("#"*45 + "\n")

try:
    led.color = (1, 0, 0)
    servo.min()
    tranca.off()
    
    print("Vigilância ativa...")

    while True:
        ret, frame = cap.read()
        if not ret: 
            continue

        tempo_atual = time.time()
        gray_original = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        
        clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
        gray_detec = clahe.apply(gray_original)
        
        rostos = face_cascade.detectMultiScale(gray_detec, 1.1, 8, minSize=(100, 100))

        # Lógica de Reset por tempo (Timeout)
        if contador_real > 0 and (tempo_atual - ultimo_tempo_sucesso > TEMPO_RESET):
            print("\n[!] Tempo expirado. Zerando contador de confiança.")
            contador_real = 0

        if len(rostos) > 0:
            for (x, y, w, h) in rostos:
                if eh_rosto_real(gray_original, x, y, w, h):
                    contador_real += 1
                    ultimo_tempo_sucesso = tempo_atual # Atualiza o momento da última validação
                    print(f"   >>> Confiança da Biometria: {contador_real}/{QUADROS_NECESSARIOS}")
                
                if contador_real >= QUADROS_NECESSARIOS:
                    liberar_acesso()
                    print("\nVigilância ativa. Aguardando alvo...")
                    break 

        sleep(0.05)

except KeyboardInterrupt:
    print("\n\nEncerrando sistema...")
finally:
    cap.release()
    led.off()
    tranca.off()

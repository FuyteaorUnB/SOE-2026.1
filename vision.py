import cv2
import os
import warnings
import numpy as np
import time
import requests
from collections import deque
from skimage.feature import local_binary_pattern

warnings.filterwarnings("ignore")

TELEGRAM_TOKEN   = "INSIRA_TOKEN"
TELEGRAM_CHAT_ID = "INSIRA_ID"
PIPE_PATH        = "/tmp/gate_pipe"

if not os.path.exists(PIPE_PATH):
    os.mkfifo(PIPE_PATH)

LBP_RAIO, LBP_PONTOS, LBP_METODO = 1, 8, 'uniform'
LBP_MIN, LAP_MIN, GRAD_STD_MIN, FFT_RATIO = 6.8, 28.0, 12.0, 0.65
SCORE_MIN = 3

JANELA_FRAMES       = 10
MINIMO_REAIS_JANELA = 6
LAP_PICO_MIN        = 80
TEMPO_RESET         = 12
KERNEL_SHARP        = np.array([[-1,-1,-1], [-1,9,-1], [-1,-1,-1]])

janela_liveness   = deque(maxlen=JANELA_FRAMES)
janela_identidade = deque(maxlen=JANELA_FRAMES)
janela_lap        = deque(maxlen=JANELA_FRAMES)
ultimo_tempo      = 0.0
ultima_notificacao = 0.0

reconhecedor = cv2.face.LBPHFaceRecognizer_create()
reconhecedor.read('classificador_lbph.yml')
face_cascade = cv2.CascadeClassifier('haarcascade_frontalface_default.xml')

# CLAHE criado UMA VEZ fora do loop — antes era recriado a cada frame
clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))

cap = cv2.VideoCapture(0, cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FRAME_WIDTH,  320)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)


def enviar_telegram(foto_path):
    if "SEU_TOKEN" in TELEGRAM_TOKEN:
        return
    try:
        texto = "[ALERTA] Intruso ou tentativa de spoofing detectada!"
        requests.post(
            f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendMessage",
            data={"chat_id": TELEGRAM_CHAT_ID, "text": texto},
            timeout=3,
        )
        with open(foto_path, "rb") as f:
            requests.post(
                f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendPhoto",
                data={"chat_id": TELEGRAM_CHAT_ID},
                files={"photo": f},
                timeout=3,
            )
    except Exception:
        pass


def enviar_sinal_pipe(sinal: bytes):
    """
    Escreve um único byte no pipe IPC.

    Com o C++ mantendo o descritor aberto de forma persistente, o
    open(O_WRONLY | O_NONBLOCK) abaixo sempre encontra um leitor ativo
    e retorna imediatamente com sucesso. O byte fica no buffer do kernel
    até o poll() do C++ acordar e consumi-lo.

    CORREÇÃO: 'except OSError: pass' foi substituído por log explícito.
    Erros silenciosos impediam diagnóstico da falha de sincronia IPC.
    """
    try:
        fifo = os.open(PIPE_PATH, os.O_WRONLY | os.O_NONBLOCK)
        os.write(fifo, sinal)
        os.close(fifo)
    except OSError as e:
        # Com o C++ usando descritor persistente isso não deve ocorrer.
        # Se aparecer aqui, significa que o processo C++ não está rodando.
        print(f"[PIPE] Falha ao enviar sinal '{sinal}' pelo pipe: {e}")


def processa_frame_completo(gray_original, x, y, w, h):
    roi = gray_original[y:y+h, x:x+w]
    if roi.size == 0 or w < 60 or h < 60:
        return False, False, 0.0

    roi_redimensionada = cv2.resize(roi, (120, 120), interpolation=cv2.INTER_AREA)

    id_usuario, confianca = reconhecedor.predict(roi_redimensionada)
    e_o_fernando = (id_usuario == 1) and (confianca < 85.0)

    roi_sharp = cv2.filter2D(cv2.GaussianBlur(roi_redimensionada, (3, 3), 0), -1, KERNEL_SHARP)
    v_lbp  = np.var(local_binary_pattern(roi_sharp, LBP_PONTOS, LBP_RAIO, method=LBP_METODO))
    ok_lbp = v_lbp > LBP_MIN

    v_lap  = cv2.Laplacian(roi_redimensionada, cv2.CV_64F).var()
    ok_lap = v_lap > LAP_MIN

    sx     = cv2.Sobel(roi_redimensionada, cv2.CV_64F, 1, 0, ksize=3)
    sy     = cv2.Sobel(roi_redimensionada, cv2.CV_64F, 0, 1, ksize=3)
    v_grad = np.std(np.sqrt(sx**2 + sy**2))
    ok_grad = v_grad > GRAD_STD_MIN

    # FFT em 60x60 em vez de 120x120 — ~4x mais rápido, mesma discriminação
    roi_fft = cv2.resize(roi_redimensionada, (60, 60))
    mag     = 20 * np.log1p(np.abs(np.fft.fftshift(np.fft.fft2(roi_fft))))
    yy, xx  = np.ogrid[:60, :60]
    dist    = np.sqrt((xx - 30)**2 + (yy - 30)**2)
    e_low   = np.mean(mag[dist <= 8])
    e_high  = np.mean(mag[(dist > 8) & (dist <= 25)])
    v_fft   = e_high / (e_low + 1e-10)
    ok_fft  = v_fft > FFT_RATIO

    score   = sum([ok_lbp, ok_lap, ok_grad, ok_fft])
    eh_real = (score >= SCORE_MIN) and ok_lbp and ok_fft

    print(
        f"ID={id_usuario}(Conf:{confianca:.1f}) | "
        f"LBP={v_lbp:.1f} | LAP={v_lap:.1f} | FFT={v_fft:.3f} [{score}/4] "
        f"-> [{'REAL' if eh_real else 'FAKE'}]"
    )
    return eh_real, e_o_fernando, v_lap


print("\n" + "=" * 50)
print("   SISTEMA DE VISÃO INTEGRADO (LIVENESS + ID)")
print("=" * 50 + "\n")

try:
    while True:
        ret, frame = cap.read()
        if not ret:
            continue

        tempo_atual   = time.time()
        gray_original = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        gray_detec    = clahe.apply(gray_original)
        rostos     = face_cascade.detectMultiScale(gray_detec, 1.1, 8, minSize=(100, 100))

        if len(janela_liveness) > 0 and (tempo_atual - ultimo_tempo > TEMPO_RESET):
            janela_liveness.clear()
            janela_identidade.clear()
            janela_lap.clear()

        if len(rostos) > 0:
            x, y, w, h = rostos[0]
            eh_real, eh_fernando, lap_val = processa_frame_completo(gray_original, x, y, w, h)

            janela_liveness.append(eh_real)
            janela_identidade.append(eh_fernando)
            janela_lap.append(lap_val)
            ultimo_tempo = tempo_atual

            if len(janela_liveness) >= JANELA_FRAMES:
                frames_reais    = sum(janela_liveness)
                frames_fernando = sum(janela_identidade)
                pico_lap        = max(janela_lap)
                passou_pico     = pico_lap > LAP_PICO_MIN

                if frames_reais >= MINIMO_REAIS_JANELA and passou_pico and (frames_fernando >= MINIMO_REAIS_JANELA):
                    print("\n[ACESSO CONCEDIDO] Identidade e Liveness confirmados por tendência temporal!")
                    enviar_sinal_pipe(b'F')

                    janela_liveness.clear()
                    janela_identidade.clear()
                    janela_lap.clear()
                    time.sleep(5)

                elif frames_reais < 4 or frames_fernando < 4:
                    print("\n[ALERTA DE INTRUSO] Tendência de fraude ou usuário desconhecido!")
                    enviar_sinal_pipe(b'I')

                    if tempo_atual - ultima_notificacao > 20:
                        cv2.imwrite("intruso.jpg", frame)
                        enviar_telegram("intruso.jpg")
                        ultima_notificacao = tempo_atual

                    janela_liveness.clear()
                    janela_identidade.clear()
                    janela_lap.clear()

        time.sleep(0.15)  # era 0.05 — reduz CPU e temperatura da Pi

except KeyboardInterrupt:
    print("\nFechando visão...")
finally:
    cap.release()

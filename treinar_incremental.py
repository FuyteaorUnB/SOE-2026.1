import cv2
import os
import numpy as np
import time

# ══════════════════════════════════════════════════════════════════════════════
#  TREINO INCREMENTAL — adiciona amostras ao modelo sem reescrever do zero.
#  Usa reconhecedor.update() em vez de reconhecedor.train().
#  O .yml existente é carregado, atualizado e salvo de volta.
# ══════════════════════════════════════════════════════════════════════════════

ID_FERNANDO   = 1
AMOSTRAS      = 30          # fotos a capturar nesta sessão
MODELO        = 'classificador_lbph.yml'
PASTA_PATCH   = 'dataset_patches'

os.makedirs(PASTA_PATCH, exist_ok=True)

# ─── Verifica modelo base ─────────────────────────────────────────────────────
if not os.path.exists(MODELO):
    print(f"ERRO: {MODELO} nao encontrado.")
    print("  Rode primeiro o treinar_e_ver.py para criar o modelo base.")
    exit(1)

tamanho_antes = os.path.getsize(MODELO) / (1024 * 1024)
print(f"\n[MODELO] {MODELO} carregado ({tamanho_antes:.1f} MB)")

# ─── Descreve o ângulo/condição que está sendo adicionado ────────────────────
print()
print("Descreva em poucas palavras o angulo/condicao que vai treinar agora.")
print("Exemplo: 'cima_baixo_contraluz', 'virado_esquerda_noite', 'oculos'")
descricao = input(">>> Nome desta sessao: ").strip().replace(" ", "_") or "patch"

print()
print(f"  {AMOSTRAS} fotos serao capturadas e adicionadas ao modelo existente.")
print()

# ─── Instruções livres do usuário ─────────────────────────────────────────────
print("Posicione-se na posicao/condicao que quer treinar.")
for i in range(8, 0, -1):
    print(f"  Iniciando em {i}s...  ", end="\r", flush=True)
    time.sleep(1)
print(f"  *** CAPTURANDO {AMOSTRAS} FOTOS — MANTENHA A POSIÇÃO ***   ")
print()

# ─── Auto-detecção da câmera ─────────────────────────────────────────────────
caminho_xml = 'haarcascade_frontalface_default.xml'
if not os.path.exists(caminho_xml):
    print(f"ERRO: {caminho_xml} nao encontrado.")
    exit(1)

face_cascade = cv2.CascadeClassifier(caminho_xml)

def abrir_camera():
    for idx in range(4):
        path = f"/dev/video{idx}"
        if not os.path.exists(path):
            continue
        c = cv2.VideoCapture(path, cv2.CAP_V4L2)
        if c.isOpened():
            ret, frame = c.read()
            if ret and frame is not None:
                print(f"[CAM] Camera em {path}")
                return c
            c.release()
    return None

cap = abrir_camera()
if cap is None:
    print("ERRO: Nenhuma camera encontrada. Verifique com: sudo fuser /dev/video*")
    exit(1)

cap.set(cv2.CAP_PROP_FRAME_WIDTH,  320)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)

# ─── Captura ──────────────────────────────────────────────────────────────────
novas_faces  = []
novos_labels = []
contagem     = 0
sem_rosto    = 0

try:
    while contagem < AMOSTRAS:
        ret, frame = cap.read()
        if not ret:
            continue

        gray   = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        rostos = face_cascade.detectMultiScale(
            gray, scaleFactor=1.1, minNeighbors=6, minSize=(80, 80)
        )

        if len(rostos) == 0:
            sem_rosto += 1
            if sem_rosto % 25 == 0:
                print("  [!] Rosto nao detectado — ajuste posicao/distancia")
            time.sleep(0.05)
            continue

        sem_rosto = 0
        x, y, w, h = rostos[0]
        roi = gray[y:y+h, x:x+w]
        roi = cv2.resize(roi, (120, 120), interpolation=cv2.INTER_AREA)

        nome = f"{descricao}_{contagem:03d}.jpg"
        cv2.imwrite(os.path.join(PASTA_PATCH, nome), roi)

        novas_faces.append(roi)
        novos_labels.append(ID_FERNANDO)
        contagem += 1

        barra = "#" * contagem + "." * (AMOSTRAS - contagem)
        print(f"  [{barra}] {contagem}/{AMOSTRAS}", end="\r", flush=True)
        time.sleep(0.15)

finally:
    cap.release()

# ─── Update incremental do modelo ─────────────────────────────────────────────
print(f"\n\n[MODELO] Aplicando {contagem} novas amostras ao modelo existente...")

reconhecedor = cv2.face.LBPHFaceRecognizer_create()
reconhecedor.read(MODELO)
reconhecedor.update(novas_faces, np.array(novos_labels))
reconhecedor.write(MODELO)

tamanho_depois = os.path.getsize(MODELO) / (1024 * 1024)

print(f"  OK {MODELO} atualizado")
print(f"  Tamanho: {tamanho_antes:.1f} MB  →  {tamanho_depois:.1f} MB")
print(f"  Fotos desta sessao salvas em: ./{PASTA_PATCH}/")
print()
print("  Teste agora com o vision.py.")
print("  Se ainda falhar nessa posicao, rode este script mais uma vez.")
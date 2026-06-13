import cv2
import os
import numpy as np
import time
import shutil

# ══════════════════════════════════════════════════════════════════════════════
#  TREINO MULTI-ÂNGULO — 7 fases × 30 fotos = 210 amostras
#
#  Por que 7 fases?
#  O LBPH NÃO é invariante a rotação. Ele só reconhece o que viu no treino.
#  Se você treinou só frontal, qualquer inclinação de 10° sobe a confiança
#  acima do limiar e você vira "intruso". Este script cobre os ângulos reais
#  que acontecem na abordagem de uma porta, incluindo o caso crítico de
#  cima para baixo com contraluz (fase 7).
# ══════════════════════════════════════════════════════════════════════════════

ID_FERNANDO       = 1
AMOSTRAS_POR_FASE = 30
PASTA_FOTOS       = "dataset_fernando"

FASES = [
    {
        "titulo": "FASE 1/6 — FRONTAL NEUTRO (referência base)",
        "instrucoes": [
            "  1. Sente ou fique de pé DIRETO de frente para a câmera",
            "  2. A câmera deve estar na ALTURA EXATA dos seus olhos",
            "  3. Distância: ~40 a 50 cm (um braço estendido)",
            "  4. Olhe diretamente para a LENTE, expressão neutra",
            "  5. NÃO mova cabeça, ombros nem distância durante a captura",
        ],
        "espera": 8,
    },
    {
        "titulo": "FASE 2/6 — ROTAÇÃO ESQUERDA ~15°",
        "instrucoes": [
            "  1. Gire APENAS A CABEÇA para a esquerda ~15°",
            "     (como se olhasse para o canto esquerdo do cômodo)",
            "  2. Seu nariz fica apontado levemente para a esquerda",
            "  3. Os dois olhos ainda aparecem na câmera, só assimétricos",
            "  4. Mesma distância da câmera",
            "  5. MANTENHA esse ângulo FIXO durante toda a fase",
        ],
        "espera": 10,
    },
    {
        "titulo": "FASE 3/6 — ROTAÇÃO DIREITA ~15°",
        "instrucoes": [
            "  1. Gire APENAS A CABEÇA para a direita ~15°",
            "     (espelho da fase anterior, mas para o lado direito)",
            "  2. Seu nariz fica apontado levemente para a direita",
            "  3. MANTENHA esse ângulo FIXO durante toda a fase",
        ],
        "espera": 10,
    },
    {
        "titulo": "FASE 4/6 — QUEIXO LEVANTADO ~10°",
        "instrucoes": [
            "  1. Levante o queixo ~10° para cima",
            "     (como se você olhasse para algo levemente acima da câmera)",
            "  2. Não exagere — apenas um leve ângulo, câmera ainda captura o rosto",
            "  3. Simula quando a câmera está mais alta que o seu rosto na porta",
            "  4. MANTENHA a inclinação FIXO durante toda a fase",
        ],
        "espera": 10,
    },
    {
        "titulo": "FASE 5/6 — MAIS PERTO (~25 cm)",
        "instrucoes": [
            "  1. Aproxime o rosto até ~25 a 30 cm da câmera",
            "     (bem mais perto do que o normal)",
            "  2. Rosto de FRENTE — sem girar",
            "  3. Simula quando você inclina para o sensor na porta",
            "  4. Se a detecção travar (rosto muito grande), afaste 2-3 cm",
        ],
        "espera": 10,
    },
    {
        "titulo": "FASE 6/6 — MAIS LONGE + SORRISO (~60 cm)",
        "instrucoes": [
            "  1. Afaste o rosto para ~55 a 65 cm da câmera",
            "     (mais longe do que o normal)",
            "  2. Dê um LEVE sorriso natural (não forçado)",
            "  3. Rosto de frente",
            "  4. Simula a distância de abordagem normal vindo de longe",
        ],
        "espera": 10,
    },
    {
        "titulo": "FASE 7/7 — CIMA PARA BAIXO CONTRA A LUZ (angulo critico)",
        "instrucoes": [
            "  1. POSIÇÃO DA LUZ: vire-se de costas para a janela ou lâmpada",
            "     principal do cômodo — a luz deve ficar ATRÁS de você.",
            "     Isso cria o contraluz que faz o rosto ficar subexposto.",
            "",
            "  2. INCLINAÇÃO DA CABEÇA: abaixe o queixo ~20-25° para baixo",
            "     (como se você estivesse olhando para o chão levemente).",
            "     A câmera deve ver mais a sua testa do que o queixo.",
            "",
            "  3. DISTÂNCIA: ~40 a 50 cm da câmera (distância normal).",
            "",
            "  4. Durante a captura, mantenha o queixo abaixado e a luz",
            "     SEMPRE atrás de você — não corrija a iluminação.",
            "",
            "  [!] Se o rosto nao for detectado: incline menos o queixo",
            "      ou afaste 5 cm da camera ate aparecer '...' no progresso.",
        ],
        "espera": 14,
    },
]

# ─── Limpeza do dataset antigo ────────────────────────────────────────────────
if os.path.exists(PASTA_FOTOS):
    shutil.rmtree(PASTA_FOTOS)
os.makedirs(PASTA_FOTOS)

caminho_xml = 'haarcascade_frontalface_default.xml'
if not os.path.exists(caminho_xml):
    print(f"ERRO: {caminho_xml} não encontrado.")
    exit(1)

face_cascade = cv2.CascadeClassifier(caminho_xml)

# ─── Auto-detecção da câmera (/dev/video0 até /dev/video3) ───────────────────
def abrir_camera():
    for idx in range(4):
        path = f"/dev/video{idx}"
        if not os.path.exists(path):
            continue
        c = cv2.VideoCapture(path, cv2.CAP_V4L2)
        if c.isOpened():
            ret, frame = c.read()
            if ret and frame is not None:
                print(f"[CAM] Camera encontrada em {path}")
                return c
            c.release()
    return None

cap = abrir_camera()
if cap is None:
    print("ERRO: Nenhuma camera encontrada em /dev/video0-3.")
    print("  Verifique com: sudo fuser /dev/video*")
    print("  Mate processos presos: sudo pkill -f vision.py")
    exit(1)

cap.set(cv2.CAP_PROP_FRAME_WIDTH,  320)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)

faces_treino   = []
labels_treino  = []
contagem_total = 0

# ─── Cabeçalho ────────────────────────────────────────────────────────────────
print("\n" + "=" * 60)
print("   TREINO MULTI-ANGULO — SISTEMA DE SEGURANÇA")
print(f"   {len(FASES)} fases x {AMOSTRAS_POR_FASE} fotos = {len(FASES)*AMOSTRAS_POR_FASE} amostras totais")
print("=" * 60)
print()
print("  LEIA ANTES DE COMEÇAR:")
print("  → Siga as instruções de CADA FASE com atenção")
print("  → Cada fase captura um ângulo diferente do seu rosto")
print("  → Manter o ângulo FIXO durante a fase é essencial")
print("  → O tempo de espera entre fases é para você se reposicionar")
print("=" * 60)

# ─── Loop de fases ────────────────────────────────────────────────────────────
try:
    for fase_idx, fase in enumerate(FASES):
        print(f"\n{'─' * 60}")
        print(f"  {fase['titulo']}")
        print(f"{'─' * 60}")
        print("  O QUE FAZER AGORA:")
        for linha in fase["instrucoes"]:
            print(linha)
        print()

        # Contagem regressiva — tempo para o usuário se posicionar
        for i in range(fase["espera"], 0, -1):
            print(f"  Posicione-se... {i}s  ", end="\r", flush=True)
            time.sleep(1)
        print(f"  *** CAPTURANDO {AMOSTRAS_POR_FASE} FOTOS — MANTENHA A POSIÇÃO ***   ")
        print()

        contagem_fase   = 0
        sem_rosto_count = 0

        while contagem_fase < AMOSTRAS_POR_FASE:
            ret, frame = cap.read()
            if not ret:
                continue

            gray   = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            rostos = face_cascade.detectMultiScale(
                gray, scaleFactor=1.1, minNeighbors=6, minSize=(80, 80)
            )

            if len(rostos) == 0:
                sem_rosto_count += 1
                if sem_rosto_count % 20 == 0:
                    print("  [!] Rosto nao detectado — ajuste posicao/distancia")
                time.sleep(0.05)
                continue

            sem_rosto_count = 0
            x, y, w, h = rostos[0]
            roi = gray[y:y+h, x:x+w]
            roi_redimensionada = cv2.resize(roi, (120, 120), interpolation=cv2.INTER_AREA)

            nome_arquivo = f"f{fase_idx+1}_a{contagem_fase:03d}.jpg"
            cv2.imwrite(os.path.join(PASTA_FOTOS, nome_arquivo), roi_redimensionada)

            faces_treino.append(roi_redimensionada)
            labels_treino.append(ID_FERNANDO)

            contagem_fase  += 1
            contagem_total += 1

            barra = "#" * contagem_fase + "." * (AMOSTRAS_POR_FASE - contagem_fase)
            print(f"  [{barra}] {contagem_fase}/{AMOSTRAS_POR_FASE}  (total: {contagem_total})", end="\r", flush=True)

            time.sleep(0.15)

        print(f"\n  OK Fase {fase_idx + 1} concluida! ({contagem_fase} fotos)")

finally:
    cap.release()

# ─── Treino LBPH ─────────────────────────────────────────────────────────────
print(f"\n{'=' * 60}")
print(f"  Treinando LBPH com {contagem_total} amostras...")
reconhecedor = cv2.face.LBPHFaceRecognizer_create()
reconhecedor.train(faces_treino, np.array(labels_treino))
reconhecedor.write('classificador_lbph.yml')

tamanho_yml = os.path.getsize('classificador_lbph.yml') / (1024 * 1024)
print(f"  OK classificador_lbph.yml salvo ({tamanho_yml:.1f} MB)")
print(f"  OK Fotos em: ./{PASTA_FOTOS}/")
print(f"  OK Total de amostras: {contagem_total}")
print("=" * 60)
print()
print("  PROXIMO PASSO:")
print("  Execute o vision.py e verifique se a deteccao melhorou.")
print("  Confianca esperada durante reconhecimento: abaixo de 85")
print()
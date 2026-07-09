#!/usr/bin/env bash
# atualizar_negativos.sh - Pipeline automatico da classe negativa:
#   1) baixa N rostos sinteticos de thispersondoesnotexist.com
#   2) recorta os rostos (Haar) para dataset_outros/
#   3) retreina o modelo (dataset_fernando + dataset_amigo + dataset_outros)
#   4) mostra um resumo para conferencia
#
# Uso:   ./atualizar_negativos.sh [quantidade]
# Ex.:   ./atualizar_negativos.sh 60      (padrao: 60)

set -u
N="${1:-60}"
TMP="fotos_baixadas"
DEST="dataset_outros"

echo "=================================================="
echo "  ATUALIZACAO AUTOMATICA DA CLASSE NEGATIVA (N=$N)"
echo "=================================================="

# --- 0) Garante as ferramentas compiladas -------------------------------
if [ ! -x ./extrair_rostos ]; then
    echo "[build] Compilando extrair_rostos..."
    g++ -O2 -std=c++17 $(pkg-config --cflags opencv4) \
        extrair_rostos.cpp -o extrair_rostos $(pkg-config --libs opencv4) \
        || { echo "ERRO ao compilar extrair_rostos"; exit 1; }
fi
if [ ! -x ./treinar ]; then
    echo "[build] Compilando treinar..."
    make treinar || { echo "ERRO ao compilar treinar"; exit 1; }
fi

# --- 1) Baixa os rostos -------------------------------------------------
echo
echo "[1/3] Baixando $N rostos sinteticos..."
bash baixar_rostos.sh "$N" "$TMP" || { echo "ERRO no download"; exit 1; }
baixadas=$(ls -1 "$TMP"/*.jpg 2>/dev/null | wc -l)

# --- 2) Recorta os rostos para dataset_outros ---------------------------
echo
echo "[2/3] Recortando rostos para $DEST/ ..."
antes=$(ls -1 "$DEST"/*.jpg 2>/dev/null | wc -l)
./extrair_rostos "$TMP"/*.jpg
depois=$(ls -1 "$DEST"/*.jpg 2>/dev/null | wc -l)
novos=$(( depois - antes ))

# --- 3) Retreina o modelo -----------------------------------------------
echo
echo "[3/3] Retreinando o modelo..."
./treinar || { echo "ERRO no treino"; exit 1; }

# --- Resumo -------------------------------------------------------------
echo
echo "=================================================="
echo "  RESUMO"
echo "=================================================="
echo "  Imagens baixadas .......... $baixadas   (em $TMP/)"
echo "  Negativos antes ........... $antes"
echo "  Negativos novos ........... $novos"
echo "  Negativos agora ........... $depois   (em $DEST/)"
if [ -f modelo_lbph.txt ]; then
    amostras=$(head -n1 modelo_lbph.txt | awk '{print $3}')
    echo "  Amostras no modelo ........ $amostras   (modelo_lbph.txt)"
fi
echo "=================================================="
echo "Pronto. Para conferir as imagens:  ls $DEST/ | head"

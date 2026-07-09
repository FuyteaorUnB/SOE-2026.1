#!/usr/bin/env bash
# baixar_rostos.sh - baixa N rostos sinteticos de thispersondoesnotexist.com
#                    para montar a classe negativa (dataset_outros).
#
# Uso:   ./baixar_rostos.sh [quantidade] [pasta_destino]
# Ex.:   ./baixar_rostos.sh 60 fotos_baixadas
#
# Depois, extraia os rostos recortados para dataset_outros:
#   ./extrair_rostos fotos_baixadas/*.jpg

set -u

N="${1:-60}"                       # quantos rostos baixar (padrao 60)
DEST="${2:-fotos_baixadas}"        # pasta de destino
URL="https://thispersondoesnotexist.com/random-person.jpeg"
UA="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36"

mkdir -p "$DEST"
echo "Baixando $N rostos sinteticos para $DEST/ ..."

declare -A vistos                  # hashes ja baixados (evita duplicatas)
baixados=0
tentativas=0
maxTentativas=$(( N * 5 + 20 ))    # trava de seguranca contra loop infinito

while [ "$baixados" -lt "$N" ] && [ "$tentativas" -lt "$maxTentativas" ]; do
    tentativas=$(( tentativas + 1 ))
    arquivo="$DEST/tpdne_$(printf '%03d' $(( baixados + 1 ))).jpg"

    curl -s -m 15 -A "$UA" -o "$arquivo" "$URL"

    # 1) o arquivo existe e nao esta vazio?
    if [ ! -s "$arquivo" ]; then
        echo "[!] Download vazio, tentando de novo..."
        rm -f "$arquivo"; sleep 2; continue
    fi

    # 2) e realmente um JPEG? (nao uma pagina de erro/HTML)
    if ! file "$arquivo" | grep -qi 'JPEG'; then
        echo "[!] Nao veio uma imagem JPEG, tentando de novo..."
        rm -f "$arquivo"; sleep 2; continue
    fi

    # 3) e um rosto novo? (descarta duplicatas do cache)
    hash="$(md5sum "$arquivo" | cut -d' ' -f1)"
    if [ -n "${vistos[$hash]:-}" ]; then
        echo "[~] Rosto repetido (cache), aguardando um novo..."
        rm -f "$arquivo"; sleep 2; continue
    fi
    vistos[$hash]=1

    baixados=$(( baixados + 1 ))
    echo "[$baixados/$N] $arquivo"
    sleep 1.5                      # troca de rosto no servidor + educado
done

echo
if [ "$baixados" -lt "$N" ]; then
    echo "Parei em $baixados de $N (limite de tentativas). Rode de novo p/ completar."
else
    echo "Concluido: $baixados rostos em $DEST/"
fi
echo "Proximo passo:  ./extrair_rostos $DEST/*.jpg"

#!/usr/bin/env python3
# baixar_diversidade.py
# Baixa rostos de varias etnias (sub-representadas nos negativos atuais) para
# balancear a classe negativa. Usa a lib 'icrawler' (mecanismos Bing/Baidu),
# que e bem mais robusta do que raspar o Google na mao.
#
# Instalar uma vez:   pip3 install icrawler
# Rodar:              python3 baixar_diversidade.py
# Depois:             ./extrair_rostos fotos_diversas/*/*.jpg
#                     ./treinar
#
# Observacao: sao fotos de pessoas reais (privacidade) - use so localmente,
# como classe negativa, e nao versione (ja esta no .gitignore via fotos_*).

import os
from icrawler.builtin import BingImageCrawler, BaiduImageCrawler

# Etnias/termos para cobrir o que a StyleGAN (FFHQ) e os negativos locais nao cobrem.
# "face portrait" / "rosto" ajuda a vir rosto unico em vez de foto de corpo/grupo.
TERMOS = [
    "japanese person face portrait",
    "korean person face portrait",
    "chinese person face portrait",
    "southeast asian face portrait",
    "sub saharan african face portrait",
    "west african person face portrait",
    "south asian indian face portrait",
    "middle eastern arab face portrait",
    "indigenous person face portrait",
]

POR_TERMO = 40            # quantas imagens por busca
DEST      = "fotos_diversas"
ENGINE    = "bing"        # "bing" (padrao) ou "baidu" (bom p/ rostos do leste asiatico)

def crawler_para(pasta):
    if ENGINE == "baidu":
        return BaiduImageCrawler(storage={"root_dir": pasta})
    return BingImageCrawler(storage={"root_dir": pasta})

def main():
    os.makedirs(DEST, exist_ok=True)
    for termo in TERMOS:
        pasta = os.path.join(DEST, termo.replace(" ", "_"))
        os.makedirs(pasta, exist_ok=True)
        print(f"\n=== Baixando: '{termo}' -> {pasta} ===")
        try:
            crawler_para(pasta).crawl(keyword=termo, max_num=POR_TERMO)
        except Exception as e:
            print(f"[!] Falha em '{termo}': {e}")

    print("\nConcluido. Proximos passos:")
    print("  ./extrair_rostos fotos_diversas/*/*.jpg   # recorta os rostos p/ dataset_outros")
    print("  ./treinar                                 # retreina com os negativos balanceados")

if __name__ == "__main__":
    main()

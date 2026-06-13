# Portaria Eletrônica com Reconhecimento Facial — SOE 2026.1 (PC3)

Sistema de controle de acesso residencial biométrico, embarcado em **Raspberry Pi 3 Model B**, com processamento local (*Edge Computing*), prova de vida (*liveness*) multimétrica e defesa contra ataques de apresentação (fotos/telas).

A arquitetura é um **sistema concorrente de dois processos** que se comunicam por um *pipe* nomeado (FIFO):

- **Núcleo de Controle (C++)** — aciona a trava solenoide de 12 V e o LED RGB; usa *threads*, `mutex`, sinais e `poll()`.
- **Pipeline de Visão (Python/OpenCV)** — captura, detecção facial (Haar), reconhecimento (LBPH) e *liveness* (LBP + Laplaciano + Sobel + FFT).

## Estrutura do repositório

```
.
├── README.md
├── .gitignore
├── requirements.txt
├── config.example.py        # modelo de configuração (copiar para config.py)
├── src/
│   ├── main.cpp             # núcleo de controle (C++)
│   ├── vision.py            # pipeline de visão + IPC
│   ├── treinar_e_ver.py     # treino multi-ângulo (7 fases, 210 amostras)
│   └── treinar_incremental.py
├── models/
│   └── haarcascade_frontalface_default.xml
└── docs/
    └── Trabalho_SOE.pdf     # relatório PC3
```

> **Observação:** o classificador `classificador_lbph.yml` (~20 MB, dados biométricos)
> **não** é versionado. Gere-o localmente com `treinar_e_ver.py` ou disponibilize
> como *Release asset* / Git LFS, se necessário.

## Hardware

- Raspberry Pi 3 Model B
- Trava solenoide 12 V + fonte 12 V
- Driver Darlington (BC548 + TIP31C), diodo *flyback* 1N4007, capacitor 100 µF
- LED RGB de cátodo comum (3 × resistor 330 Ω), resistor de *pull-down* 10 kΩ
- Webcam USB
- **GND comum** entre a fonte de 12 V e a Pi (essencial)

GPIOs: trava = `17`; LED R/G/B = `22 / 27 / 10`.

## Dependências (software)

```bash
pip install -r requirements.txt
```

`cv2.face` (LBPH) exige **opencv-contrib-python** (não basta o `opencv-python`).

## Como compilar e rodar

```bash
# 1) Núcleo C++ (cria o FIFO e controla o hardware)
g++ -O2 -pthread src/main.cpp -o gate_control
sudo ./gate_control

# 2) Em outro terminal: pipeline de visão
python3 src/vision.py
```

Antes de rodar a visão, gere o modelo:

```bash
python3 src/treinar_e_ver.py     # cria classificador_lbph.yml
```

## Configuração de segredos

As credenciais do Telegram **não** ficam no código. Copie o modelo e preencha:

```bash
cp config.example.py config.py   # config.py está no .gitignore
```

## Trabalhos futuros (ver relatório)

- Calibração do LBPH (classe negativa `ID=2`, normalização de iluminação da ROI)
- Migração para *embeddings* faciais por rede neural
- Senha temporária para entregadores (caso iFood)
- Correção do barramento de terra comum e soldagem definitiva

// Pipeline de visão integrado (Liveness + Reconhecimento Facial)

#include "visao_core.hpp"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <deque>
#include <map>
#include <string>
#include <ctime>
#include <cstdio>
#include <thread>
#include <chrono>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

// Configurações do Telegram
static const std::string TELEGRAM_TOKEN   = "INSERIR_TOKEN";
static const std::string TELEGRAM_CHAT_ID = "INSERIR_ID";

static const std::string PIPE_PATH = "/tmp/gate_pipe";

// Limiares de classificação
static double LBP_MIN      = 3.7;
static double LAP_MIN      = 28.0;
static double GRAD_STD_MIN = 12.0;
static double FFT_RATIO    = 0.65;
static int    SCORE_MIN    = 3;
static double CONF_MAX     = 38.0;

// Rotulo da classe negativa; qualquer outro rotulo reconhecido e usuario autorizado.
static const int LABEL_NEGATIVO = 2;

static const int    JANELA_FRAMES       = 10;
static const int    MINIMO_REAIS_JANELA = 6;   // quadros reais (liveness) na janela
static const int    MINIMO_AUT_JANELA   = 9;   // quadros da mesma identidade autorizada
static const double LAP_PICO_MIN        = 80.0;
static const double TEMPO_RESET         = 12.0;

void enviarTelegram(const std::string& fotoPath)
{
    if (TELEGRAM_TOKEN.find("COLOQUE_SEU") != std::string::npos) return;
    std::string base = "https://api.telegram.org/bot" + TELEGRAM_TOKEN;
    std::string msg =
        "curl -s -m 4 -X POST \"" + base + "/sendMessage\""
        " -d chat_id=" + TELEGRAM_CHAT_ID +
        " --data-urlencode text=\"[ALERTA] Intruso ou spoofing detectado!\" >/dev/null 2>&1";
    (void)std::system(msg.c_str());
    
    std::string foto =
        "curl -s -m 6 -X POST \"" + base + "/sendPhoto\""
        " -F chat_id=" + TELEGRAM_CHAT_ID +
        " -F photo=@" + fotoPath + " >/dev/null 2>&1";
    (void)std::system(foto.c_str());
}

void enviarSinalPipe(char sinal)
{
    int fifo = open(PIPE_PATH.c_str(), O_WRONLY | O_NONBLOCK);
    if (fifo < 0) {
        std::cerr << "[ERRO] Falha ao comunicar via FIFO.\n";
        return;
    }
    ssize_t w = write(fifo, &sinal, 1);
    (void)w;
    close(fifo);
}

bool abrirCamera(cv::VideoCapture& cap)
{
    for (int idx = 0; idx < 4; ++idx) {
        cap.open(idx, cv::CAP_V4L2);
        if (cap.isOpened()) {
            cv::Mat teste;
            if (cap.read(teste) && !teste.empty()) {
                std::cout << "[CAM] Dispositivo /dev/video" << idx << " ativo.\n";
                return true;
            }
            cap.release();
        }
    }
    return false;
}

int main(int argc, char** argv)
{
    bool modoCalibrar = (argc > 1 && std::string(argv[1]) == "--calibrar");

    if (access(PIPE_PATH.c_str(), F_OK) != 0)
        mkfifo(PIPE_PATH.c_str(), 0666);

    soe::LBPHModel modelo;
    if (!modelo.carregar("modelo_lbph.txt")) {
        std::cerr << "Erro: modelo_lbph.txt nao encontrado.\n";
        return 1;
    }

    cv::CascadeClassifier faceCascade;
    if (!faceCascade.load("haarcascade_frontalface_default.xml")) {
        std::cerr << "Erro: haarcascade_frontalface_default.xml nao encontrado.\n";
        return 1;
    }

    cv::VideoCapture cap;
    if (!abrirCamera(cap)) {
        std::cerr << "Erro: Nenhuma camera disponivel.\n";
        return 1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  320);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));

    std::deque<bool>   janelaLive;
    std::deque<int>    janelaId;    // rótulo autorizado por quadro (0 = nao autorizado)
    std::deque<double> janelaLap;
    double ultimoTempo = 0.0, ultimaNotif = 0.0;

    std::cout << "Inicializando pipeline de visao...\n";
    if (modoCalibrar) std::cout << "[MODO CALIBRACAO] Nenhuma acao sera disparada.\n";

    cv::Mat frame, gray, grayDetec;

    while (true) {
        if (!cap.read(frame) || frame.empty()) continue;
        double agora = (double)cv::getTickCount() / cv::getTickFrequency();

        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        clahe->apply(gray, grayDetec);

        std::vector<cv::Rect> rostos;
        faceCascade.detectMultiScale(grayDetec, rostos, 1.1, 8, 0, cv::Size(100, 100));

        if (!janelaLive.empty() && (agora - ultimoTempo > TEMPO_RESET)) {
            janelaLive.clear(); janelaId.clear(); janelaLap.clear();
        }

        if (!rostos.empty()) {
            cv::Rect r = rostos[0];
            cv::Mat face = soe::preprocessarFace(gray, r);
            if (face.empty()) { cv::waitKey(1); continue; }

            auto [idUsuario, conf] = modelo.predict(face);
            bool ehAutorizado = (idUsuario >= 1) && (idUsuario != LABEL_NEGATIVO) && (conf < CONF_MAX);

            double vLbp  = soe::lbpUniformVar(face);

            cv::Mat lap; cv::Laplacian(face, lap, CV_64F);
            cv::Scalar mLap, sLap; cv::meanStdDev(lap, mLap, sLap);
            double vLap = sLap[0] * sLap[0];

            cv::Mat sx, sy;
            cv::Sobel(face, sx, CV_64F, 1, 0, 3);
            cv::Sobel(face, sy, CV_64F, 0, 1, 3);
            cv::Mat magg; cv::magnitude(sx, sy, magg);
            cv::Scalar mG, sG; cv::meanStdDev(magg, mG, sG);
            double vGrad = sG[0];

            double vFft = soe::fftRatio(face);

            bool okLbp  = vLbp  > LBP_MIN;
            bool okLap  = vLap  > LAP_MIN;
            bool okGrad = vGrad > GRAD_STD_MIN;
            bool okFft  = vFft  > FFT_RATIO;
            
            int  score  = (int)okLbp + okLap + okGrad + okFft;
            bool ehReal = (score >= SCORE_MIN) && okLbp && okFft;

            std::printf("ID=%d(Conf:%.1f) | LBP=%.1f%c LAP=%.1f%c GRD=%.1f%c FFT=%.3f%c [%d/4] -> [%s]\n",
                idUsuario, conf,
                vLbp,  okLbp ?'O':'X',
                vLap,  okLap ?'O':'X',
                vGrad, okGrad?'O':'X',
                vFft,  okFft ?'O':'X',
                score, ehReal ? "REAL" : "FAKE");
            std::fflush(stdout);

            janelaLive.push_back(ehReal);
            janelaId.push_back(ehAutorizado ? idUsuario : 0);
            janelaLap.push_back(vLap);
            
            if ((int)janelaLive.size() > JANELA_FRAMES) { 
                janelaLive.pop_front(); janelaId.pop_front(); janelaLap.pop_front(); 
            }
            ultimoTempo = agora;

            if ((int)janelaLive.size() >= JANELA_FRAMES) {
                int framesReais    = 0; for (bool b : janelaLive) framesReais += b;
                // framesAut = maior nº de quadros da mesma identidade autorizada na janela
                std::map<int,int> contId;
                for (int lab : janelaId) if (lab != 0) contId[lab]++;
                int framesAut = 0, rotuloDom = 0;
                for (const auto& kv : contId)
                    if (kv.second > framesAut) { framesAut = kv.second; rotuloDom = kv.first; }

                double picoLap     = 0; for (double v : janelaLap) picoLap = std::max(picoLap, v);
                bool passouPico    = picoLap > LAP_PICO_MIN;
                bool abriria = (framesReais >= MINIMO_REAIS_JANELA && passouPico && framesAut >= MINIMO_AUT_JANELA);

                if (modoCalibrar) {
                    std::printf("   [JANELA] reais=%d/%d  aut=%d/%d (rotulo %d)  picoLap=%.0f  -> %s\n",
                        framesReais, JANELA_FRAMES, framesAut, JANELA_FRAMES, rotuloDom, picoLap,
                        abriria ? "ABRIRIA" : "nao abre");
                    std::fflush(stdout);
                }
                else if (abriria) {
                    std::cout << "[ACESSO CONCEDIDO] Identidade e liveness confirmados.\n";
                    enviarSinalPipe('F');
                    janelaLive.clear(); janelaId.clear(); janelaLap.clear();
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
                else if (framesReais < 4 || framesAut < 4) {
                    std::cout << "[ALERTA] Fraude ou usuario nao reconhecido.\n";
                    enviarSinalPipe('I');
                    if (agora - ultimaNotif > 20.0) {
                        cv::imwrite("intruso.jpg", frame);
                        enviarTelegram("intruso.jpg");
                        ultimaNotif = agora;
                    }
                    janelaLive.clear(); janelaId.clear(); janelaLap.clear();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    cap.release();
    return 0;
}

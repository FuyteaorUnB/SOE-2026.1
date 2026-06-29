// Treino do classificador LBPH

#include "visao_core.hpp"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

// Adiciona uma face normalizada ao modelo
static void adicionarFace(soe::LBPHModel& modelo, const cv::Mat& roiGray, int label)
{
    cv::Mat resized;
    cv::resize(roiGray, resized, cv::Size(soe::FACE_SIZE, soe::FACE_SIZE), 0, 0, cv::INTER_AREA);
    cv::Mat norm = soe::tanTriggs(resized);
    modelo.add(norm, label);
}

// Carrega imagens de um diretório
static int carregarPasta(soe::LBPHModel& modelo, const std::string& pasta, int label)
{
    if (!fs::exists(pasta)) return 0;
    int n = 0;
    for (const auto& e : fs::directory_iterator(pasta)) {
        std::string p = e.path().string();
        std::string ext = e.path().extension().string();
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") continue;
        
        cv::Mat img = cv::imread(p, cv::IMREAD_GRAYSCALE);
        if (img.empty()) continue;
        
        adicionarFace(modelo, img, label);
        n++;
    }
    return n;
}

struct Fase { std::string titulo; int espera; };

static const std::vector<Fase> FASES = {
    {"FASE 1/7 - FRONTAL NEUTRO", 8},
    {"FASE 2/7 - ROTACAO ESQUERDA", 10},
    {"FASE 3/7 - ROTACAO DIREITA", 10},
    {"FASE 4/7 - QUEIXO LEVANTADO", 10},
    {"FASE 5/7 - MAIS PERTO", 10},
    {"FASE 6/7 - MAIS LONGE + SORRISO", 10},
    {"FASE 7/7 - CONTRA A LUZ", 14},
};
static const int AMOSTRAS_POR_FASE = 30;

static bool abrirCamera(cv::VideoCapture& cap) {
    for (int idx = 0; idx < 4; ++idx) {
        cap.open(idx, cv::CAP_V4L2);
        if (cap.isOpened()) {
            cv::Mat t;
            if (cap.read(t) && !t.empty()) { 
                std::cout << "[CAM] Indice " << idx << "\n"; 
                return true; 
            }
            cap.release();
        }
    }
    return false;
}

static int capturarMultiAngulo(soe::LBPHModel& modelo)
{
    fs::create_directories("dataset_fernando");

    cv::CascadeClassifier cascade;
    if (!cascade.load("haarcascade_frontalface_default.xml")) {
        std::cerr << "Erro: haarcascade_frontalface_default.xml nao encontrado.\n";
        return -1;
    }
    
    cv::VideoCapture cap;
    if (!abrirCamera(cap)) { 
        std::cerr << "Erro: camera nao detectada.\n"; 
        return -1; 
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 320);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);

    int total = 0;
    cv::Mat frame, gray;

    for (size_t fi = 0; fi < FASES.size(); ++fi) {
        std::cout << "\n" << FASES[fi].titulo << "\n";
        
        for (int s = FASES[fi].espera; s > 0; --s) {
            std::cout << "Iniciando em " << s << "s... \r" << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << "\nCapturando " << AMOSTRAS_POR_FASE << " fotos...\n";

        int cont = 0, semRosto = 0;
        while (cont < AMOSTRAS_POR_FASE) {
            if (!cap.read(frame) || frame.empty()) continue;
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            
            std::vector<cv::Rect> rostos;
            cascade.detectMultiScale(gray, rostos, 1.1, 6, 0, cv::Size(80, 80));
            
            if (rostos.empty()) {
                if (++semRosto % 20 == 0) std::cout << "[!] Rosto nao detectado\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            semRosto = 0;
            
            cv::Rect r = rostos[0];
            cv::Mat roi = gray(r & cv::Rect(0,0,gray.cols,gray.rows));
            cv::Mat roi120; 
            cv::resize(roi, roi120, cv::Size(soe::FACE_SIZE, soe::FACE_SIZE), 0,0, cv::INTER_AREA);

            char nome[128];
            std::snprintf(nome, sizeof(nome), "dataset_fernando/f%zu_a%03d.jpg", fi+1, cont);
            cv::imwrite(nome, roi120);

            adicionarFace(modelo, roi120, 1);
            cont++; total++;
            
            std::cout << "[" << cont << "/" << AMOSTRAS_POR_FASE << "] total=" << total << "\r" << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        std::cout << "\nFase concluida.\n";
    }
    cap.release();
    return total;
}

int main(int argc, char** argv)
{
    bool capturar = (argc > 1 && std::string(argv[1]) == "--capturar");
    soe::LBPHModel modelo;

    if (capturar) {
        std::cout << "Iniciando treino por captura multi-angulo...\n";
        int n = capturarMultiAngulo(modelo);
        if (n <= 0) return 1;
        std::cout << "\nCapturadas " << n << " amostras.\n";
    } else {
        std::cout << "Iniciando treino a partir dos datasets...\n";
        int n1 = carregarPasta(modelo, "dataset_fernando", 1);
        int n2 = carregarPasta(modelo, "dataset_outros",   2);
        
        std::cout << "dataset_fernando/ : " << n1 << " amostras\n";
        std::cout << "dataset_outros/   : " << n2 << " amostras\n";
        
        if (n1 == 0) {
            std::cerr << "Erro: dataset vazio. Use --capturar.\n";
            return 1;
        }
    }

    if (!modelo.salvar("modelo_lbph.txt")) {
        std::cerr << "Erro ao salvar modelo_lbph.txt\n";
        return 1;
    }
    
    std::cout << "\nModelo salvo com " << modelo.hists.size() << " amostras.\n";
    return 0;
}
// Extrai rostos de imagens e salva em dataset_outros/

#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc < 2) { 
        std::cerr << "Uso: ./extrair_rostos imagem1 [imagem2 ...]\n"; 
        return 1; 
    }

    cv::CascadeClassifier cascade;
    if (!cascade.load("haarcascade_frontalface_default.xml")) {
        std::cerr << "Erro: haarcascade_frontalface_default.xml nao encontrado.\n";
        return 1;
    }
    
    fs::create_directories("dataset_outros");

    // Continua a numeracao a partir dos arquivos ja existentes (nao sobrescreve).
    int total = 0;
    for (const auto& e : fs::directory_iterator("dataset_outros"))
        if (e.path().extension() == ".jpg") total++;
    int inicial = total;

    for (int a = 1; a < argc; ++a) {
        cv::Mat img = cv::imread(argv[a], cv::IMREAD_GRAYSCALE);
        if (img.empty()) { 
            std::cerr << "[!] Falha ao abrir: " << argv[a] << "\n"; 
            continue; 
        }

        std::vector<cv::Rect> rostos;
        cascade.detectMultiScale(img, rostos, 1.1, 5, 0, cv::Size(40, 40));

        for (size_t i = 0; i < rostos.size(); ++i) {
            cv::Mat roi = img(rostos[i]);
            cv::Mat r120; 
            cv::resize(roi, r120, cv::Size(120, 120), 0, 0, cv::INTER_AREA);
            
            char nome[160];
            std::snprintf(nome, sizeof(nome), "dataset_outros/outro_%03d.jpg", total);
            cv::imwrite(nome, r120);
            total++;
        }
        std::cout << argv[a] << ": " << rostos.size() << " rostos extraidos\n";
    }
    
    std::cout << "Total de " << (total - inicial) << " novos rostos ("
              << total << " no total) em dataset_outros/\n";
    return 0;
}

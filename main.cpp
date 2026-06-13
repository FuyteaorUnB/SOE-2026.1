#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <string>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <signal.h>

#define PIPE_PATH "/tmp/gate_pipe"

#define PIN_TRANCA "17"
#define PIN_LED_R  "22"
#define PIN_LED_G  "27"
#define PIN_LED_B  "10"

bool rodando = true;
bool portaAberta = false;
bool alertaIntruso = false;

std::mutex mtx;
std::mutex hwMtx;

int cacheR = -1, cacheG = -1, cacheB = -1, cacheT = -1;

void escreverGPIO(std::string pin, int valor) {
    std::lock_guard<std::mutex> lock(hwMtx);

    if (pin == PIN_LED_R && valor == cacheR) return;
    if (pin == PIN_LED_G && valor == cacheG) return;
    if (pin == PIN_LED_B && valor == cacheB) return;
    if (pin == PIN_TRANCA && valor == cacheT) return;

    if (pin == PIN_LED_R) cacheR = valor;
    if (pin == PIN_LED_G) cacheG = valor;
    if (pin == PIN_LED_B) cacheB = valor;
    if (pin == PIN_TRANCA) cacheT = valor;

    std::string estado = (valor == 1) ? "dh" : "dl";
    std::string comando = "sudo pinctrl set " + pin + " op " + estado + " > /dev/null 2>&1";
    std::system(comando.c_str());
}

void tratarCtrlC(int sinal) {
    rodando = false;
}

void inicializarHardware() {
    std::cout << "[HARDWARE] Inicializando estados (LED Vermelho)..." << std::endl;
    escreverGPIO(PIN_TRANCA, 0);
    escreverGPIO(PIN_LED_R, 1);
    escreverGPIO(PIN_LED_G, 0);
    escreverGPIO(PIN_LED_B, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// CORREÇÃO PRINCIPAL
// Antes: o loop abria, lia e fechava o fd a cada 40 ms.
//        Isso criava janelas de ~40 ms sem nenhum leitor ativo.
//        O Python usa O_WRONLY | O_NONBLOCK, que retorna ENXIO imediatamente
//        se não existe leitor → o byte 'F' nunca entrava no buffer do kernel.
//
// Agora: o fd é aberto UMA SÓ VEZ e mantido aberto durante toda a execução.
//        poll() bloqueia de forma eficiente (sem busy-wait) até chegarem dados
//        ou o timeout de 100 ms expirar (para checar a flag "rodando").
//        Com um leitor permanente, o open(O_WRONLY|O_NONBLOCK) do Python sempre
//        tem sucesso e o kernel guarda o byte no buffer da FIFO até o poll()
//        acordar este thread.
// ─────────────────────────────────────────────────────────────────────────────
void threadMonitorPipe() {
    int fd = -1;

    // Abre o descritor UMA VEZ. Repete só se o pipe ainda não existir.
    while (rodando) {
        fd = open(PIPE_PATH, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!rodando) return;

    std::cout << "[IPC] Pipe aberto de forma persistente (fd=" << fd
              << "). Aguardando sinais do Python..." << std::endl;

    while (rodando) {
        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        // Bloqueia até 100 ms - eficiente, sem gastar CPU.
        // O timeout curto permite verificar "rodando" com frequência.
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) {
            if (errno == EINTR) continue; // Interrompido por sinal, não é erro
            std::cerr << "[IPC] Erro inesperado em poll(): " << errno << std::endl;
            break;
        }
        if (ret == 0) continue; // Timeout normal - nenhum dado ainda

        if (pfd.revents & POLLIN) {
            char comando;
            ssize_t n = read(fd, &comando, 1);

            if (n == 1) {
                if (comando == 'F') {
                    { std::lock_guard<std::mutex> lock(mtx); portaAberta = true; }
                    std::cout << "\n[C++] Sinal IPC: Fernando Autorizado! Abrindo Solenoide..." << std::endl;

                    escreverGPIO(PIN_TRANCA, 1);
                    std::this_thread::sleep_for(std::chrono::seconds(4));
                    escreverGPIO(PIN_TRANCA, 0);

                    std::cout << "[C++] Fechadura trancada." << std::endl;
                    { std::lock_guard<std::mutex> lock(mtx); portaAberta = false; }
                }
                else if (comando == 'I') {
                    { std::lock_guard<std::mutex> lock(mtx); alertaIntruso = true; }
                    std::cout << "\n[C++] Sinal IPC: Alerta de Intruso Ativado!" << std::endl;

                    std::this_thread::sleep_for(std::chrono::seconds(6));

                    { std::lock_guard<std::mutex> lock(mtx); alertaIntruso = false; }
                }
            }
            // n == 0 - EOF: todos os escritores fecharam o lado write do pipe.
        }
    }

    if (fd >= 0) close(fd);
}

void threadControleLED() {
    bool estadoPisca = false;
    while (rodando) {
        bool statusPorta, statusAlerta;
        {
            std::lock_guard<std::mutex> lock(mtx);
            statusPorta  = portaAberta;
            statusAlerta = alertaIntruso;
        }

        if (statusPorta) {
            escreverGPIO(PIN_LED_R, 0); escreverGPIO(PIN_LED_G, 1); escreverGPIO(PIN_LED_B, 0);
        }
        else if (statusAlerta) {
            if (estadoPisca) {
                escreverGPIO(PIN_LED_R, 1); escreverGPIO(PIN_LED_G, 0); escreverGPIO(PIN_LED_B, 0);
            } else {
                escreverGPIO(PIN_LED_R, 0); escreverGPIO(PIN_LED_G, 0); escreverGPIO(PIN_LED_B, 1);
            }
            estadoPisca = !estadoPisca;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        else {
            escreverGPIO(PIN_LED_R, 1); escreverGPIO(PIN_LED_G, 0); escreverGPIO(PIN_LED_B, 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    signal(SIGINT,  tratarCtrlC);
    signal(SIGTERM, tratarCtrlC);

    std::cout << "   ==================================================    " << std::endl;
    std::cout << "     SISTEMA DE SEGURANÇA RESIDENCIAL EMBARCADO      " << std::endl;
    std::cout << "   ==================================================    " << std::endl;

    unlink(PIPE_PATH);
    mkfifo(PIPE_PATH, 0666);
    chmod(PIPE_PATH, 0666);  // Força permissões ignorando o umask do root (que resultaria em 0644)

    inicializarHardware();

    std::thread tMonitor(threadMonitorPipe);
    std::thread tLED(threadControleLED);

    std::cout << "[CORE] Pronto para rodar!\n" << std::endl;

    tMonitor.join();
    tLED.join();

    std::cout << "\n[SOE] Desligando hardware e limpando buffers de forma limpa..." << std::endl;

    cacheR = cacheG = cacheB = cacheT = -1;
    escreverGPIO(PIN_TRANCA, 0);
    escreverGPIO(PIN_LED_R, 0);
    escreverGPIO(PIN_LED_G, 0);
    escreverGPIO(PIN_LED_B, 0);
    unlink(PIPE_PATH);

    return 0;
}
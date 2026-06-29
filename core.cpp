// Controle de baixo nível da fechadura e LEDs

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>

#define PIPE_PATH "/tmp/gate_pipe"

#define PIN_TRANCA "17"
#define PIN_LED_R  "22"
#define PIN_LED_G  "27"
#define PIN_LED_B  "10"

#define PRIO_MONITOR 50
#define CPU_MONITOR   3
#define CPU_LED       2

std::atomic<bool> rodando{true};
bool portaAberta   = false;
bool alertaIntruso = false;

std::mutex mtx;
std::mutex hwMtx;

int cacheR = -1, cacheG = -1, cacheB = -1, cacheT = -1;

void escreverGPIO(const std::string& pin, int valor) {
    std::lock_guard<std::mutex> lock(hwMtx);

    if (pin == PIN_LED_R && valor == cacheR) return;
    if (pin == PIN_LED_G && valor == cacheG) return;
    if (pin == PIN_LED_B && valor == cacheB) return;
    if (pin == PIN_TRANCA && valor == cacheT) return;

    if (pin == PIN_LED_R) cacheR = valor;
    if (pin == PIN_LED_G) cacheG = valor;
    if (pin == PIN_LED_B) cacheB = valor;
    if (pin == PIN_TRANCA) cacheT = valor;

    std::string estado  = (valor == 1) ? "dh" : "dl";
    std::string comando = "sudo pinctrl set " + pin + " op " + estado + " > /dev/null 2>&1";
    (void)std::system(comando.c_str());
}

void tratarSinal(int) { rodando = false; }

void inicializarHardware() {
    std::cout << "[HW] Inicializando componentes..." << std::endl;
    escreverGPIO(PIN_TRANCA, 0);
    escreverGPIO(PIN_LED_R, 1);
    escreverGPIO(PIN_LED_G, 0);
    escreverGPIO(PIN_LED_B, 0);
}

bool configurarTempoReal(int prioridade, int cpu, const char* nome) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0)
        std::cerr << "[RT] (" << nome << ") Falha ao definir afinidade CPU " << cpu << std::endl;

    struct sched_param sp;
    std::memset(&sp, 0, sizeof(sp));
    sp.sched_priority = prioridade;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        std::cerr << "[RT] (" << nome << ") SCHED_FIFO falhou. Operando em modo normal." << std::endl;
        return false;
    }
    return true;
}

void threadMonitorPipe() {
    configurarTempoReal(PRIO_MONITOR, CPU_MONITOR, "monitor_pipe");

    int fd = -1;
    while (rodando) {
        fd = open(PIPE_PATH, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!rodando) return;

    std::cout << "[IPC] Pipe aberto. Aguardando sinais..." << std::endl;

    while (rodando) {
        struct pollfd pfd;
        pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;

        int ret = poll(&pfd, 1, 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[IPC] Erro no poll: " << errno << std::endl;
            break;
        }
        if (ret == 0) continue;

        if (pfd.revents & POLLIN) {
            char comando;
            ssize_t n = read(fd, &comando, 1);
            if (n == 1) {
                if (comando == 'F') {
                    { std::lock_guard<std::mutex> lk(mtx); portaAberta = true; }
                    std::cout << "[CORE] Acesso autorizado. Abrindo trava." << std::endl;
                    escreverGPIO(PIN_TRANCA, 1);
                    std::this_thread::sleep_for(std::chrono::seconds(4));
                    escreverGPIO(PIN_TRANCA, 0);
                    { std::lock_guard<std::mutex> lk(mtx); portaAberta = false; }
                }
                else if (comando == 'I') {
                    { std::lock_guard<std::mutex> lk(mtx); alertaIntruso = true; }
                    std::cout << "[CORE] Alerta: Intruso detectado!" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(6));
                    { std::lock_guard<std::mutex> lk(mtx); alertaIntruso = false; }
                }
            }
        }
    }
    if (fd >= 0) close(fd);
}

void threadControleLED() {
    configurarTempoReal(PRIO_MONITOR - 10, CPU_LED, "led_periodico");

    const long PERIODO_NS = 100L * 1000L * 1000L;
    struct timespec proximo;
    clock_gettime(CLOCK_MONOTONIC, &proximo);

    bool estadoPisca = false;
    int  contador    = 0;

    while (rodando) {
        bool statusPorta, statusAlerta;
        { std::lock_guard<std::mutex> lk(mtx);
          statusPorta  = portaAberta;
          statusAlerta = alertaIntruso; }

        if (statusPorta) {
            escreverGPIO(PIN_LED_R, 0); escreverGPIO(PIN_LED_G, 1); escreverGPIO(PIN_LED_B, 0);
        }
        else if (statusAlerta) {
            if (++contador >= 2) {
                estadoPisca = !estadoPisca;
                contador = 0;
                if (estadoPisca) { escreverGPIO(PIN_LED_R,1); escreverGPIO(PIN_LED_G,0); escreverGPIO(PIN_LED_B,0); }
                else             { escreverGPIO(PIN_LED_R,0); escreverGPIO(PIN_LED_G,0); escreverGPIO(PIN_LED_B,1); }
            }
        }
        else {
            escreverGPIO(PIN_LED_R, 1); escreverGPIO(PIN_LED_G, 0); escreverGPIO(PIN_LED_B, 0);
            contador = 0; estadoPisca = false;
        }

        proximo.tv_nsec += PERIODO_NS;
        while (proximo.tv_nsec >= 1000000000L) { proximo.tv_nsec -= 1000000000L; proximo.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &proximo, nullptr);
    }
}

int main() {
    signal(SIGINT,  tratarSinal);
    signal(SIGTERM, tratarSinal);

    std::cout << "Iniciando Sistema de Controle de Acesso..." << std::endl;

    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0)
        std::cout << "[RT] mlockall(): Memoria em RAM travada com sucesso." << std::endl;
    else
        std::cerr << "[RT] Falha no mlockall(). (Requer sudo)" << std::endl;

    unlink(PIPE_PATH);
    mkfifo(PIPE_PATH, 0666);
    chmod(PIPE_PATH, 0666);

    inicializarHardware();

    std::thread tMonitor(threadMonitorPipe);
    std::thread tLED(threadControleLED);

    tMonitor.join();
    tLED.join();

    std::cout << "\n[CORE] Desligando sistema..." << std::endl;
    cacheR = cacheG = cacheB = cacheT = -1;
    escreverGPIO(PIN_TRANCA, 0);
    escreverGPIO(PIN_LED_R, 0);
    escreverGPIO(PIN_LED_G, 0);
    escreverGPIO(PIN_LED_B, 0);
    unlink(PIPE_PATH);
    munlockall();
    
    return 0;
}
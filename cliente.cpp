
// cliente.cpp - Simulador de parqueadero (50 placas/50 puestos) con entradas/salidas aleatorias
// Envia líneas CSV al servidor: plate,spot,YYYY-MM-DDTHH:MM:SS,event\n
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <cstring>
#include <ctime>

static const int DEFAULT_PORT = 8080;
static const char* DEFAULT_HOST = "127.0.0.1";
static const int TOTAL_PUESTOS = 50;   // cantidad de puestos en el parqueadero
static const int TOTAL_PLACAS = 50;  // cantidad de placas distintas
static const int DEFAULT_EVENTS = 350; // cantidad de eventos a enviar (entrada/salida aleatorios)
static const int MAX_DELAY_SEC = 3;  // retardo aleatorio entre eventos (0..3s)

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
#else
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <unistd.h>
  using socket_t = int;
#endif

static std::string now_iso8601_local() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    std::time_t t = system_clock::to_time_t(tp);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &t);
#else
    local_tm = *std::localtime(&t);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
                  local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
    return std::string(buf);
}

static std::string random_plate(std::mt19937 &rng) {
    // Formato de placas AAA999 (3 letras + 3 números)
    std::uniform_int_distribution<int> A('A','Z');
    std::uniform_int_distribution<int> D(0,9);
    std::string p; p.reserve(6);
    for(int i=0;i<3;++i) p.push_back((char)A(rng));
    for(int i=0;i<3;++i) p.push_back((char)('0'+D(rng)));
    return p;
}

// Genera TOTAL_PLACAS placas únicas
static std::vector<std::string> generate_unique_plates(std::mt19937 &rng, int n = TOTAL_PLACAS) {
    std::unordered_set<std::string> s;
    s.reserve(n*2);
    while((int)s.size() < n) {
        s.insert(random_plate(rng));
    }
    return std::vector<std::string>(s.begin(), s.end());
}

// Devuelve un spot libre aleatorio (1..TOTAL_PUESTOS)
static int random_free_spot(std::mt19937 &rng, const std::vector<bool> &occupied) {
    std::vector<int> free;
    free.reserve(TOTAL_PUESTOS);
    for(int i=1;i<=TOTAL_PUESTOS;++i) if(!occupied[i]) free.push_back(i);
    if (free.empty()) return -1;
    std::uniform_int_distribution<size_t> pick(0, free.size()-1);
    return free[pick(rng)];
}

// Devuelve un spot ocupado aleatorio
static int random_occupied_spot(std::mt19937 &rng, const std::vector<bool> &occupied) {
    std::vector<int> occ;
    occ.reserve(TOTAL_PUESTOS);
    for(int i=1;i<=TOTAL_PUESTOS;++i) if(occupied[i]) occ.push_back(i);
    if (occ.empty()) return -1;
    std::uniform_int_distribution<size_t> pick(0, occ.size()-1);
    return occ[pick(rng)];
}

// Devuelve una placa libre (no estacionada actualmente)
static std::string random_free_plate(std::mt19937 &rng, const std::vector<std::string> &plates,
                                     const std::unordered_set<std::string> &parked) {
    std::vector<std::string> freep;
    freep.reserve(plates.size());
    for (auto &p : plates) if (parked.find(p) == parked.end()) freep.push_back(p);
    if (freep.empty()) return std::string();
    std::uniform_int_distribution<size_t> pick(0, freep.size()-1);
    return freep[pick(rng)];
}

int main(int argc, char** argv) {
    // Configuración por argumentos: host port events
    const char* host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    int events_to_send = DEFAULT_EVENTS;
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);
    if (argc >= 4) events_to_send = std::atoi(argv[3]);
    if (events_to_send <= 0) events_to_send = DEFAULT_EVENTS;

#ifdef _WIN32
    WSADATA wsa; if (WSAStartup(MAKEWORD(2,2), &wsa)!=0){ std::cerr<<"WSAStartup falló\n"; return 1; }
#endif
    socket_t sock = (socket_t)socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (sock == INVALID_SOCKET) { std::cerr << "socket() falló\n"; WSACleanup(); return 1; }
#else
    if (sock < 0) { perror("socket"); return 1; }
#endif

    sockaddr_in serv{}; serv.sin_family = AF_INET; serv.sin_port = htons(port);
#ifdef _WIN32
    if (inet_pton(AF_INET, host, &serv.sin_addr) != 1) { std::cerr << "Dirección inválida\n"; closesocket(sock); WSACleanup(); return 1; }
    if (connect(sock, (sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR) { std::cerr << "connect() falló\n"; closesocket(sock); WSACleanup(); return 1; }
#else
    if (inet_pton(AF_INET, host, &serv.sin_addr) <= 0) { std::cerr << "Dirección inválida\n"; close(sock); return 1; }
    if (connect(sock, (sockaddr*)&serv, sizeof(serv)) < 0) { perror("connect"); close(sock); return 1; }
#endif

    // RNG
    std::mt19937 rng((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> delayDist(0, MAX_DELAY_SEC);

    // Placas únicas
    auto plates = generate_unique_plates(rng, TOTAL_PLACAS);

    // Estado del parqueadero
    std::vector<bool> occupied(TOTAL_PUESTOS+1, false); // index 1..TOTAL_PUESTOS
    std::unordered_map<int, std::string> spot_to_plate; // spot -> plate
    std::unordered_set<std::string> parked; // placas actualmente adentro
    spot_to_plate.reserve(TOTAL_PUESTOS);
    parked.reserve(TOTAL_PLACAS);

    // Simulación de eventos
    for (int ev=0; ev<events_to_send; ++ev) {
        int current_occupancy = (int)parked.size();
        bool can_enter = current_occupancy < TOTAL_PUESTOS;
        bool can_exit  = current_occupancy > 0;

        // decidir acción
        bool do_enter = false;
        if (can_enter && can_exit) {
            std::uniform_real_distribution<double> prob(0.0,1.0);
            do_enter = (prob(rng) < 0.6);
        } else if (can_enter && !can_exit) {
            do_enter = true; // vacío: solo entradas
        } else if (!can_enter && can_exit) {
            do_enter = false; // lleno: solo salidas
        } else {
            // ni entrar ni salir: debería ser caso imposible, espera un momento
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        std::string iso = now_iso8601_local();
        std::string line;

        if (do_enter) {
            // elegir spot libre y placa libre
            int spot = random_free_spot(rng, occupied);
            if (spot == -1) continue; // sin espacio
            std::string plate = random_free_plate(rng, plates, parked);
            if (plate.empty()) {
                // todas las placas ya están adentro; fuerza salida en próxima iteración
                continue;
            }
            // marcar estado
            occupied[spot] = true;
            spot_to_plate[spot] = plate;
            parked.insert(plate);

            line = plate + "," + std::to_string(spot) + "," + iso + ",entrada\n";
        } else {
            // salida: elegir spot ocupado aleatorio
            int spot = random_occupied_spot(rng, occupied);
            if (spot == -1) continue; // no hay nadie adentro
            std::string plate = spot_to_plate[spot];
            if (plate.empty()) {
                // estado inconsistente (no debería ocurrir)
                continue;
            }
            // liberar estado
            occupied[spot] = false;
            spot_to_plate.erase(spot);
            parked.erase(plate);

            line = plate + "," + std::to_string(spot) + "," + iso + ",salida\n";
        }

        // enviar
#ifdef _WIN32
        int sent = send(sock, line.c_str(), (int)line.size(), 0);
        if (sent == SOCKET_ERROR) std::cerr << "send() falló" << std::endl;
#else
        ssize_t sent = send(sock, line.c_str(), line.size(), 0);
        if (sent < 0) perror("send");
#endif
        std::cout << "[client] " << (do_enter?"ENTRADA":"SALIDA ") << ": " << line;

        std::this_thread::sleep_for(std::chrono::seconds(delayDist(rng)));
    }

#ifdef _WIN32
    closesocket(sock); WSACleanup();
#else
    close(sock);
#endif
    std::cout << "[client] Conexión cerrada" << std::endl;
    return 0;
}

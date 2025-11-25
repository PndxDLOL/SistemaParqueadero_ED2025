#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <ctime>

static const int PORT = 8080;

#pragma pack(push,1)
struct CarEvent {
    char        plate[16];
    int32_t     spot;
    long long   timestamp_ms;
    int32_t     event_type; // 0=entrada, 1=salida
};
#pragma pack(pop)

static bool parse_iso8601_to_epoch_ms(const std::string& s, long long& out_ms) {
    if (s.size() < 19) return false; // "YYYY-MM-DDTHH:MM:SS"
    if (!(std::isdigit((unsigned char)s[0]) && std::isdigit((unsigned char)s[1]) &&
          std::isdigit((unsigned char)s[2]) && std::isdigit((unsigned char)s[3]) &&
          s[4]=='-' && s[7]=='-' && (s[10]=='T' || s[10]=='t') &&
          s[13]==':' && s[16]==':')) return false;

    int Y = std::stoi(s.substr(0,4));
    int m = std::stoi(s.substr(5,2));
    int d = std::stoi(s.substr(8,2));
    int H = std::stoi(s.substr(11,2));
    int M = std::stoi(s.substr(14,2));
    int S = std::stoi(s.substr(17,2));

    std::tm tm{}; tm.tm_isdst = -1;
    tm.tm_year = Y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = H;
    tm.tm_min  = M;
    tm.tm_sec  = S;

    // Sufijo de zona
    bool has_suffix = (s.size() >= 20);
    if (has_suffix) {
        char c = s[19];
        if (c == 'Z' || c == 'z') {
            // Interpretar tm como UTC exacto
        #ifdef _WIN32
            // _mkgmtime64: tm (UTC) -> epoch (UTC)
            __time64_t utc = _mkgmtime64(&tm);
            if (utc == -1) return false;
            out_ms = (long long)utc * 1000LL;
            return true;
        #else
            time_t utc = timegm(&tm); // GNU/BSD
            if (utc == (time_t)-1) return false;
            out_ms = (long long)utc * 1000LL;
            return true;
        #endif
        } else if (c == '+' || c == '-') {
            if (s.size() < 25) return false; // HH:MM
            int sign = (c == '+') ? +1 : -1;
            int oh = std::stoi(s.substr(20,2));
            int om = std::stoi(s.substr(23,2));
            long tz_offset_sec = sign * (oh * 3600 + om * 60);
            // Truco: interpretar tm como UTC, luego restar offset: epoch_utc = timegm(tm) - offset
        #ifdef _WIN32
            __time64_t as_utc = _mkgmtime64(&tm); // trata tm como UTC
            if (as_utc == -1) return false;
            __time64_t utc = as_utc - tz_offset_sec;
            out_ms = (long long)utc * 1000LL;
            return true;
        #else
            time_t as_utc = timegm(&tm);
            if (as_utc == (time_t)-1) return false;
            time_t utc = as_utc - tz_offset_sec;
            out_ms = (long long)utc * 1000LL;
            return true;
        #endif
        }
    }

    // Sin sufijo: asumir hora LOCAL
#ifdef _WIN32
    __time64_t loc = _mktime64(&tm); // local -> epoch local
    if (loc == -1) return false;
    out_ms = (long long)loc * 1000LL;
    return true;
#else
    time_t loc = mktime(&tm);
    if (loc == (time_t)-1) return false;
    out_ms = (long long)loc * 1000LL;
    return true;
#endif
}

static inline void trim(std::string &s){
    size_t a = s.find_first_not_of(" \t\r");
    size_t b = s.find_last_not_of(" \t\r");
    if (a==std::string::npos) { s.clear(); return; }
    s = s.substr(a, b-a+1);
}

// ================== Enlaces a la librer�a ==================
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #include <windows.h>
  typedef int (__cdecl *pl_init_t)(const char*);
  typedef int (__cdecl *pl_append_t)(const char*, int, long long, int);
  typedef unsigned long long (__cdecl *pl_count_t)();
  typedef unsigned long long (__cdecl *pl_tail_t)(unsigned long long, void*);
  typedef void (__cdecl *pl_close_t)();
#else
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <unistd.h>
  extern "C" {
    int pl_init(const char* path);
    int pl_append(const char* plate, int spot, long long timestamp_ms, int event_type);
    unsigned long long pl_count();
    unsigned long long pl_tail(unsigned long long n, void* buffer);
    void pl_close();
  }
#endif
// ===========================================================

int main(){
#ifdef _WIN32
    // Cargar DLL din�micamente
    HMODULE hLib = LoadLibraryA("parking.dll");
    if (!hLib) {
        std::cerr << "[server] No se pudo cargar parking.dll (col�cala junto al exe)." << std::endl;
        return 1;
    }
    pl_init_t   p_init   = (pl_init_t)  GetProcAddress(hLib, "pl_init");
    pl_append_t p_append = (pl_append_t)GetProcAddress(hLib, "pl_append");
    pl_close_t  p_close  = (pl_close_t) GetProcAddress(hLib, "pl_close");
    if (!p_init || !p_append || !p_close) {
        std::cerr << "[server] Funciones no encontradas en parking.dll" << std::endl;
        return 1;
    }
    if (p_init("parking_events.bin") != 0) {
        std::cerr << "[server] Error inicializando almacenamiento" << std::endl;
        return 1;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa)!=0) {
        std::cerr << "WSAStartup fall�\n"; return 1;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s==INVALID_SOCKET){ std::cerr<<"socket() fall�, err="<<WSAGetLastError()<<"\n"; WSACleanup(); return 1; }

    // Exclusividad del puerto (mejor pr�ctica en Windows)
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));

    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(PORT);
    if (bind(s, (sockaddr*)&addr, sizeof(addr))==SOCKET_ERROR){
        std::cerr<<"bind() fall�. err="<<WSAGetLastError()<<"\n"; closesocket(s); WSACleanup(); return 1;
    }
    if (listen(s, 4)==SOCKET_ERROR){
        std::cerr<<"listen() fall�. err="<<WSAGetLastError()<<"\n"; closesocket(s); WSACleanup(); return 1;
    }
    std::cout << "[server] Escuchando en puerto " << PORT << "...\n";

    for(;;){
        sockaddr_in cli{}; int clen=sizeof(cli);
        SOCKET c = accept(s,(sockaddr*)&cli,&clen);
        if (c==INVALID_SOCKET){ std::cerr<<"accept() fall�, err="<<WSAGetLastError()<<"\n"; continue; }

        char buf[1024]; std::string acc; acc.reserve(4096);
        int n;
        while ((n=recv(c, buf, sizeof(buf), 0))>0){
            acc.append(buf, buf+n);
            size_t pos=0;
            while(true){
                size_t nl = acc.find('\n', pos);
                if (nl==std::string::npos) break;
                std::string line = acc.substr(pos, nl-pos);
                pos = nl + 1;
                if (line.empty()) continue;

                std::stringstream ss(line);
                std::string plate, spotStr, isoStr, evStr;
                if (!std::getline(ss, plate, ',')) continue;
                if (!std::getline(ss, spotStr, ',')) continue;
                if (!std::getline(ss, isoStr, ',')) continue;
                std::getline(ss, evStr);

                trim(plate); trim(spotStr); trim(isoStr); trim(evStr);

                int spot = 0; long long ts_ms = 0; int etype = (evStr=="salida")?1:0;
                try { spot = std::stoi(spotStr); } catch(...) { continue; }

                if (!parse_iso8601_to_epoch_ms(isoStr, ts_ms)) {
                    std::cerr << "[server] Fecha/hora inv�lida: " << isoStr << "\n";
                    continue;
                }

                if (p_append(plate.c_str(), spot, ts_ms, etype)!=0)
                    std::cerr << "[server] Error guardando: " << line << "\n";
                else
                    std::cout << "[server] Guardado: " << plate << ", spot=" << spot
                              << ", ts_ms=" << ts_ms << " (" << isoStr << "), type=" << evStr << "\n";
            }
            if (pos>0) acc.erase(0,pos);
        }
        closesocket(c);
        std::cout << "[server] Conexi�n cerrada\n";
    }

    p_close(); closesocket(s); WSACleanup(); FreeLibrary(hLib);
    return 0;

#else
    if (pl_init("parking_events.bin") != 0) {
        std::cerr << "[server] No se pudo inicializar almacenamiento\n"; return 1;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0); if (s<0){ perror("socket"); return 1; }
    int opt=1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(PORT);
    if (bind(s,(sockaddr*)&addr,sizeof(addr))<0){ perror("bind"); return 1; }
    if (listen(s,4)<0){ perror("listen"); return 1; }
    std::cout << "[server] Escuchando en puerto " << PORT << "...\n";
    for(;;){
        sockaddr_in cli{}; socklen_t clen=sizeof(cli); int c=accept(s,(sockaddr*)&cli,&clen); if (c<0){ perror("accept"); continue; }
        char buf[1024]; std::string acc; acc.reserve(4096); ssize_t n;
        while((n=recv(c, buf, sizeof(buf), 0))>0){
            acc.append(buf, buf+n);
            size_t pos=0;
            while(true){
                size_t nl = acc.find('\n', pos); if(nl==std::string::npos) break;
                std::string line=acc.substr(pos, nl-pos); pos=nl+1;
                if (line.empty()) continue;

                std::stringstream ss(line);
                std::string plate, spotStr, isoStr, evStr;
                if(!std::getline(ss, plate, ',')) continue;
                if(!std::getline(ss, spotStr, ',')) continue;
                if(!std::getline(ss, isoStr, ',')) continue;
                std::getline(ss, evStr);

                trim(plate); trim(spotStr); trim(isoStr); trim(evStr);

                int spot=0; long long ts_ms=0; int etype=(evStr=="salida")?1:0;
                try { spot = std::stoi(spotStr); } catch(...) { continue; }
                if (!parse_iso8601_to_epoch_ms(isoStr, ts_ms)) {
                    std::cerr << "[server] Fecha/hora inv�lida: " << isoStr << "\n";
                    continue;
                }

                if (pl_append(plate.c_str(), spot, ts_ms, etype)!=0)
                    std::cerr<< "[server] Error guardando: " << line << "\n";
                else
                    std::cout<< "[server] Guardado: " << plate << ", spot="<<spot
                             << ", ts_ms="<<ts_ms<<" ("<<isoStr<<"), type="<<evStr<< "\n";
            }
            if (pos>0) acc.erase(0,pos);
        }
        if (n<0) perror("recv");
        close(c);
        std::cout << "[server] Conexi�n cerrada\n";
    }
    pl_close(); close(s); return 0;
#endif
}

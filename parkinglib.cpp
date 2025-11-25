#include <cstdint>
#include <cstring>
#include <string>

#pragma pack(push,1)
struct CarEvent {
    char        plate[16];    //placa del carro
    int32_t     spot;         //numero de puesto  
    long long   timestamp_ms; //epoch ms
    int32_t     event_type;   //0=entrada, 1=salida
};
#pragma pack(pop)

static std::string g_path;

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #define PARKING_API extern "C" __declspec(dllexport)

  static bool lock_whole_file(HANDLE h, bool exclusive) {
      OVERLAPPED ov{};
      DWORD flags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
      return LockFileEx(h, flags, 0, MAXDWORD, MAXDWORD, &ov) != 0;
  }
  static void unlock_whole_file(HANDLE h) {
      OVERLAPPED ov{};
      UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ov);
  }

  // pl_init: crea el archivo si no existe (modo original)
  PARKING_API int pl_init(const char* path) {
      if (!path || !*path) return -1;
      g_path = path;
      HANDLE h = CreateFileA(g_path.c_str(), GENERIC_READ|GENERIC_WRITE,
                             FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE) return -2;
      CloseHandle(h);
      return 0;
  }

  // NUEVO: pl_init_readonly: NO crea archivo. Falla con -2 si no existe.
  PARKING_API int pl_init_readonly(const char* path) {
      if (!path || !*path) return -1;
      g_path = path;

      DWORD attr = GetFileAttributesA(g_path.c_str());
      if (attr == INVALID_FILE_ATTRIBUTES) return -2; // no existe

      // Comprobar que se puede abrir en lectura
      HANDLE h = CreateFileA(g_path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE) return -3;
      CloseHandle(h);
      return 0;
  }

  PARKING_API int pl_append(const char* plate, int spot, long long timestamp_ms, int event_type) {
      if (g_path.empty()) return -10;
      HANDLE h = CreateFileA(g_path.c_str(), GENERIC_WRITE,
                             FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE) return -11;
      if (!lock_whole_file(h, true)) { CloseHandle(h); return -12; }

      // posicionar al final
      LARGE_INTEGER li; li.QuadPart = 0;
      SetFilePointerEx(h, li, nullptr, FILE_END);

      CarEvent ev{};
      std::memset(&ev, 0, sizeof(ev));
      if (plate) { std::strncpy(ev.plate, plate, sizeof(ev.plate)-1); ev.plate[15] = '\0'; }
      ev.spot = (int32_t)spot;
      ev.timestamp_ms = timestamp_ms;
      ev.event_type = (int32_t)event_type;

      DWORD written = 0;
      BOOL ok = WriteFile(h, &ev, (DWORD)sizeof(ev), &written, nullptr);
      int rc = (ok && written == sizeof(ev)) ? 0 : -13;

      unlock_whole_file(h);
      CloseHandle(h);
      return rc;
  }

  PARKING_API unsigned long long pl_count() {
      if (g_path.empty()) return 0ULL;
      HANDLE h = CreateFileA(g_path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE) return 0ULL;
      LARGE_INTEGER sz; sz.QuadPart = 0;
      if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return 0ULL; }
      CloseHandle(h);
      if (sz.QuadPart <= 0) return 0ULL;
      return (unsigned long long)(sz.QuadPart / (LONGLONG)sizeof(CarEvent));
  }

  PARKING_API unsigned long long pl_tail(unsigned long long n, CarEvent* buffer) {
      if (g_path.empty() || !buffer || n == 0) return 0ULL;
      HANDLE h = CreateFileA(g_path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE) return 0ULL;
      if (!lock_whole_file(h, true)) { CloseHandle(h); return 0ULL; }

      LARGE_INTEGER sz; sz.QuadPart = 0;
      if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) { unlock_whole_file(h); CloseHandle(h); return 0ULL; }
      unsigned long long total = (unsigned long long)(sz.QuadPart / (LONGLONG)sizeof(CarEvent));
      unsigned long long count = (n > total) ? total : n;

      LARGE_INTEGER off; off.QuadPart = (LONGLONG)((total - count) * (unsigned long long)sizeof(CarEvent));
      SetFilePointerEx(h, off, nullptr, FILE_BEGIN);

      DWORD to_read = (DWORD)(count * (unsigned long long)sizeof(CarEvent));
      DWORD readBytes = 0;
      BOOL ok = ReadFile(h, buffer, to_read, &readBytes, nullptr);
      unsigned long long got = (ok) ? (readBytes / (unsigned long long)sizeof(CarEvent)) : 0ULL;

      unlock_whole_file(h);
      CloseHandle(h);
      return got;
  }

  PARKING_API void pl_close() { /* no-op */ }

#else
  // ================= POSIX =================
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <cerrno>

  #define PARKING_API extern "C"

  static int lock_fd(int fd, short type) {
      struct flock fl{};
      fl.l_type = type; // F_RDLCK o F_WRLCK
      fl.l_whence = SEEK_SET;
      fl.l_start = 0;
      fl.l_len = 0; // todo el archivo
      return fcntl(fd, F_SETLKW, &fl);
  }

  PARKING_API int pl_init(const char* path) {
      if (!path || !*path) return -1;
      g_path = path;
      int fd = open(g_path.c_str(), O_CREAT | O_RDWR, 0666);
      if (fd < 0) return -2;
      close(fd);
      return 0;
  }

  // NUEVO: read-only, no crea archivo
  PARKING_API int pl_init_readonly(const char* path) {
      if (!path || !*path) return -1;
      g_path = path;
      int fd = open(g_path.c_str(), O_RDONLY);
      if (fd < 0) {
          if (errno == ENOENT) return -2; // no existe
          return -3; // otro error de apertura
      }
      close(fd);
      return 0;
  }

  PARKING_API int pl_append(const char* plate, int spot, long long timestamp_ms, int event_type) {
      if (g_path.empty()) return -10;
      int fd = open(g_path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0666);
      if (fd < 0) return -11;
      if (lock_fd(fd, F_WRLCK) < 0) { close(fd); return -12; }

      CarEvent ev{}; std::memset(&ev, 0, sizeof(ev));
      if (plate) { std::strncpy(ev.plate, plate, sizeof(ev.plate)-1); ev.plate[15] = '\0'; }
      ev.spot = (int32_t)spot;
      ev.timestamp_ms = timestamp_ms;
      ev.event_type = (int32_t)event_type;

      ssize_t w = write(fd, &ev, sizeof(ev));
      int err = (w == (ssize_t)sizeof(ev)) ? 0 : -13;

      lock_fd(fd, F_UNLCK); close(fd); return err;
  }

  PARKING_API unsigned long long pl_count() {
      if (g_path.empty()) return 0ULL;
      struct stat st{};
      if (stat(g_path.c_str(), &st) != 0 || st.st_size <= 0) return 0ULL;
      return (unsigned long long)(st.st_size / (off_t)sizeof(CarEvent));
  }

  PARKING_API unsigned long long pl_tail(unsigned long long n, CarEvent* buffer) {
      if (g_path.empty() || !buffer || n == 0) return 0ULL;
      int fd = open(g_path.c_str(), O_RDONLY);
      if (fd < 0) return 0ULL;
      if (lock_fd(fd, F_RDLCK) < 0) { close(fd); return 0ULL; }

      struct stat st{};
      if (fstat(fd, &st) != 0 || st.st_size <= 0) { lock_fd(fd, F_UNLCK); close(fd); return 0ULL; }
      unsigned long long total = (unsigned long long)(st.st_size / (off_t)sizeof(CarEvent));
      unsigned long long count = (n > total) ? total : n;
      off_t offset = (off_t)((total - count) * (unsigned long long)sizeof(CarEvent));
      if (lseek(fd, offset, SEEK_SET) == (off_t)-1) { lock_fd(fd, F_UNLCK); close(fd); return 0ULL; }

      ssize_t to_read = (ssize_t)(count * (unsigned long long)sizeof(CarEvent));
      ssize_t r = read(fd, buffer, to_read);
      unsigned long long got = (r > 0) ? (unsigned long long)(r / (ssize_t)sizeof(CarEvent)) : 0ULL;

      lock_fd(fd, F_UNLCK); close(fd); return got;
  }

  PARKING_API void pl_close() {}
#endif

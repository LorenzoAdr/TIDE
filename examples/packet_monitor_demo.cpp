// Demo para el monitor de paquetes de tuide.
// Simula una app (puerto 5555) y un periférico (puerto 5556) en un solo proceso.
// Cada ciclo genera tráfico OUT e IN capturable con LD_PRELOAD.

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif

constexpr uint16_t kAppPort = 5555;
constexpr uint16_t kPeripheralPort = 5556;
constexpr const char* kLoopback = "127.0.0.1";

#pragma pack(push, 1)
struct HeartbeatPacket {
  uint8_t msg_type = 0x01;
  uint8_t reserved = 0;
  uint16_t sequence = 0;
};

struct SensorPacket {
  uint8_t msg_type = 0x02;
  uint8_t device_id = 0;
  uint16_t reserved = 0;
  float temperature = 0.0f;
  uint16_t status = 0;
};

struct AckPacket {
  uint8_t msg_type = 0x03;
  uint8_t ack_for_type = 0;
  uint16_t sequence = 0;
};
#pragma pack(pop)

void allow_external_debugger() {
  if (prctl(PR_SET_PTRACER, -1) != 0) {
    std::cerr << "aviso: prctl(PR_SET_PTRACER): " << std::strerror(errno) << "\n";
  }
}

int bind_udp_socket(uint16_t port) {
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return -1;
  }
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  if (inet_pton(AF_INET, kLoopback, &addr.sin_addr) != 1) {
    ::close(fd);
    return -1;
  }
  addr.sin_port = htons(port);
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool send_to(int fd, uint16_t port, const void* data, std::size_t size) {
  sockaddr_in dest {};
  dest.sin_family = AF_INET;
  inet_pton(AF_INET, kLoopback, &dest.sin_addr);
  dest.sin_port = htons(port);
  const ssize_t sent =
      sendto(fd, data, size, 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
  return sent == static_cast<ssize_t>(size);
}

ssize_t recv_pending(int fd, void* buffer, std::size_t size, sockaddr_in* src = nullptr) {
  sockaddr_in from {};
  socklen_t from_len = sizeof(from);
  const ssize_t received = recvfrom(fd, buffer, size, MSG_DONTWAIT,
                                    reinterpret_cast<sockaddr*>(&from), &from_len);
  if (src != nullptr && received > 0) {
    *src = from;
  }
  return received;
}

}  // namespace

int main() {
  allow_external_debugger();

  const int app_fd = bind_udp_socket(kAppPort);
  const int peripheral_fd = bind_udp_socket(kPeripheralPort);
  if (app_fd < 0 || peripheral_fd < 0) {
    std::cerr << "error al abrir sockets UDP: " << std::strerror(errno) << "\n";
    return 1;
  }

  std::cout << "packet_monitor_demo PID " << getpid() << "\n"
            << "  app:        " << kLoopback << ":" << kAppPort << "\n"
            << "  periferico: " << kLoopback << ":" << kPeripheralPort << "\n"
            << "  protocolo:  examples/protocols/hello_sensor.json\n"
            << "  tuide: F2 Launch -> m (monitor ON) -> Continue -> pestaña Paquetes\n"
            << std::flush;

  uint16_t sequence = 0;
  uint8_t device_id = 7;
  int tick = 0;

  while (true) {
    ++tick;
    ++sequence;

  // --- App -> periferico (OUT) ---
    if (tick % 4 == 0) {
      HeartbeatPacket heartbeat;
      heartbeat.sequence = sequence;
      send_to(app_fd, kPeripheralPort, &heartbeat, sizeof(heartbeat));
      std::cout << "[" << tick << "] OUT heartbeat -> :" << kPeripheralPort << "\n";
    } else {
      SensorPacket sensor;
      sensor.device_id = device_id;
      sensor.temperature = 18.0f + static_cast<float>(tick % 15);
      sensor.status = static_cast<uint16_t>(tick & 0x0F);
      send_to(app_fd, kPeripheralPort, &sensor, sizeof(sensor));
      std::cout << "[" << tick << "] OUT sensor temp=" << sensor.temperature
                << " -> :" << kPeripheralPort << "\n";
    }

    // --- Periferico recibe (IN en :5556) ---
    uint8_t peripheral_buf[256];
    const ssize_t peripheral_in = recv_pending(peripheral_fd, peripheral_buf, sizeof(peripheral_buf));
    if (peripheral_in > 0) {
      std::cout << "[" << tick << "] IN  periferico " << peripheral_in << " bytes en :"
                << kPeripheralPort << "\n";

      // --- Periferico responde (OUT hacia app) ---
      AckPacket ack;
      ack.ack_for_type = peripheral_buf[0];
      ack.sequence = sequence;
      send_to(peripheral_fd, kAppPort, &ack, sizeof(ack));
      std::cout << "[" << tick << "] OUT ack tipo=0x" << std::hex
                << static_cast<int>(ack.ack_for_type) << std::dec << " -> :" << kAppPort << "\n";
    }

    // --- App recibe respuesta (IN en :5555) ---
    uint8_t app_buf[256];
    const ssize_t app_in = recv_pending(app_fd, app_buf, sizeof(app_buf));
    if (app_in > 0) {
      std::cout << "[" << tick << "] IN  app " << app_in << " bytes en :" << kAppPort << "\n";
    }

    sleep(1);
    std::cout << std::flush;
  }

  return 0;
}

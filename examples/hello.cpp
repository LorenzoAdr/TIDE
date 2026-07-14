#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif

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
#pragma pack(pop)

constexpr uint16_t kUdpPort = 5555;

void allow_external_debugger() {
	if (prctl(PR_SET_PTRACER, -1) != 0) {
		std::cerr << "aviso: no se pudo permitir attach externo (prctl): " << std::strerror(errno)
		          << "\n";
	}
}

int create_udp_socket() {
	const int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -1;
	}
	int yes = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(kUdpPort);

	if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
		::close(fd);
		return -1;
	}
	return fd;
}

bool send_packet(int fd, const void *data, std::size_t size) {
	sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	dest.sin_port = htons(kUdpPort);
	const ssize_t sent =
	    sendto(fd, data, size, 0, reinterpret_cast<sockaddr *>(&dest), sizeof(dest));
	return sent == static_cast<ssize_t>(size);
}

void try_receive(int fd) {
	std::vector<uint8_t> buffer(256);
	sockaddr_in src{};
	socklen_t src_len = sizeof(src);
	const ssize_t received = recvfrom(fd, buffer.data(), buffer.size(), MSG_DONTWAIT,
	                                  reinterpret_cast<sockaddr *>(&src), &src_len);
	if (received > 0) {
		std::cout << "udp recv " << received << " bytes\n";
	} 
}

} // namespace

int main() {
	allow_external_debugger();

	const pid_t pid = getpid();  
	std::cout << "hello PID " << pid << "\n"
	          << "UDP demo en 127.0.0.1:" << kUdpPort << "\n"
	          << "Launch con packet monitor activo para capturar trafico.\n"
	          << std::flush;

	const int udp_fd = create_udp_socket();
	if (udp_fd < 0) {
		std::cerr << "no se pudo abrir socket UDP: " << std::strerror(errno) << "\n";
		return 1;
	}

	uint16_t sequence = 0;
	         
	uint8_t device_id = 1;
	int counter = 0;
	while (true) {
		++counter;
		++sequence;
		if (counter % 3 == 0) {
			HeartbeatPacket heartbeat;
			heartbeat.sequence = sequence;
			send_packet(udp_fd, &heartbeat, sizeof(heartbeat));
			std::cout << "[" << counter << "] sent heartbeat seq=" << sequence << "\n";
		} else {
			SensorPacket sensor;
			sensor.device_id = device_id;
			sensor.temperature = 20.0f + static_cast<float>(counter % 10);
			sensor.status = static_cast<uint16_t>(counter & 0x03);
			send_packet(udp_fd, &sensor, sizeof(sensor));
			std::cout << "[" << counter << "] sent sensor temp=" << sensor.temperature
			          << " device=" << static_cast<int>(device_id) << "\n";
			          
		}

		try_receive(udp_fd);

		sleep(1);
	}

	return 0;
}
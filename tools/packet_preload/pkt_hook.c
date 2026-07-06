#define _GNU_SOURCE

#include "pkt_shm.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef ssize_t (*recvfrom_fn)(int, void*, size_t, int, struct sockaddr*, socklen_t*);
typedef ssize_t (*sendto_fn)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);

static recvfrom_fn real_recvfrom = NULL;
static sendto_fn real_sendto = NULL;
static TgdbPktHeader* g_header = NULL;
static TgdbPktEntry* g_entries = NULL;
static bool g_enabled = false;
static uint32_t g_filter_src = 0;
static uint32_t g_filter_dst = 0;
static bool g_has_filter_src = false;
static bool g_has_filter_dst = false;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static bool parse_ipv4(const char* text, uint32_t* out) {
  if (text == NULL || out == NULL || text[0] == '\0') {
    return false;
  }
  struct in_addr addr;
  if (inet_pton(AF_INET, text, &addr) != 1) {
    return false;
  }
  *out = addr.s_addr;
  return true;
}

static const char* runtime_dir(void) {
  const char* dir = getenv("XDG_RUNTIME_DIR");
  if (dir != NULL && dir[0] != '\0') {
    return dir;
  }
  return "/tmp";
}

static bool ip_matches(uint32_t value, uint32_t filter, bool has_filter) {
  if (!has_filter) {
    return true;
  }
  return value == filter;
}

static bool packet_allowed(uint32_t src_ipv4, uint32_t dst_ipv4) {
  return ip_matches(src_ipv4, g_filter_src, g_has_filter_src) &&
         ip_matches(dst_ipv4, g_filter_dst, g_has_filter_dst);
}

static void record_packet(uint8_t direction, const struct sockaddr* addr, socklen_t addr_len,
                          const void* payload, size_t payload_len, uint16_t local_port_hint) {
  if (!g_enabled || g_header == NULL || g_entries == NULL || payload == NULL || payload_len == 0) {
    return;
  }
  if (payload_len > TGDB_PKT_MAX_PAYLOAD) {
    payload_len = TGDB_PKT_MAX_PAYLOAD;
  }

  uint32_t peer_ipv4 = 0;
  uint16_t peer_port = 0;
  if (addr != NULL && addr_len >= (socklen_t)sizeof(struct sockaddr_in)) {
    const struct sockaddr_in* sin = (const struct sockaddr_in*)addr;
    if (sin->sin_family == AF_INET) {
      peer_ipv4 = sin->sin_addr.s_addr;
      peer_port = ntohs(sin->sin_port);
    }
  }

  uint32_t src_ipv4 = 0;
  uint32_t dst_ipv4 = 0;
  uint16_t src_port = 0;
  uint16_t dst_port = 0;
  if (direction == TGDB_PKT_IN) {
    src_ipv4 = peer_ipv4;
    src_port = peer_port;
    dst_port = local_port_hint;
  } else {
    dst_ipv4 = peer_ipv4;
    dst_port = peer_port;
    src_port = local_port_hint;
  }

  if (!packet_allowed(src_ipv4, dst_ipv4)) {
    return;
  }

  const uint32_t index =
      atomic_fetch_add((atomic_uint*)&g_header->write_idx, 1u) % g_header->capacity;
  TgdbPktEntry* entry = &g_entries[index];
  memset(entry, 0, sizeof(*entry));
  entry->timestamp_ns = now_ns();
  entry->direction = direction;
  entry->src_port = src_port;
  entry->dst_port = dst_port;
  entry->src_ipv4 = src_ipv4;
  entry->dst_ipv4 = dst_ipv4;
  entry->payload_len = (uint16_t)payload_len;
  memcpy(entry->payload, payload, payload_len);
}

static void init_shm(void) {
  if (g_header != NULL) {
    return;
  }

  const char* disable = getenv("TGDB_PKT_DISABLE");
  if (disable != NULL && disable[0] == '1') {
    return;
  }

  uint32_t src = 0;
  uint32_t dst = 0;
  const char* filter_src = getenv("TGDB_PKT_FILTER_SRC");
  const char* filter_dst = getenv("TGDB_PKT_FILTER_DST");
  if (parse_ipv4(filter_src, &src)) {
    g_filter_src = src;
    g_has_filter_src = true;
  }
  if (parse_ipv4(filter_dst, &dst)) {
    g_filter_dst = dst;
    g_has_filter_dst = true;
  }

  char path[512];
  snprintf(path, sizeof(path), "%s/tgdb-pkt-%d.mmap", runtime_dir(), getpid());

  const size_t map_size =
      sizeof(TgdbPktHeader) + (size_t)TGDB_PKT_CAPACITY * sizeof(TgdbPktEntry);
  const int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    return;
  }
  if (ftruncate(fd, (off_t)map_size) != 0) {
    close(fd);
    return;
  }

  void* mapped = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    return;
  }

  g_header = (TgdbPktHeader*)mapped;
  memset(g_header, 0, sizeof(*g_header));
  g_header->magic = TGDB_PKT_MAGIC;
  g_header->version = TGDB_PKT_VERSION;
  g_header->capacity = TGDB_PKT_CAPACITY;
  g_header->entry_stride = (uint32_t)sizeof(TgdbPktEntry);
  g_header->pid = (uint32_t)getpid();
  g_entries = tgdb_pkt_entries(g_header);
  g_enabled = true;
}

__attribute__((constructor)) static void pkt_hook_init(void) {
  init_shm();
  real_recvfrom = (recvfrom_fn)dlsym(RTLD_NEXT, "recvfrom");
  real_sendto = (sendto_fn)dlsym(RTLD_NEXT, "sendto");
}

ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr,
                 socklen_t* addrlen) {
  if (real_recvfrom == NULL) {
    real_recvfrom = (recvfrom_fn)dlsym(RTLD_NEXT, "recvfrom");
  }
  const ssize_t result =
      real_recvfrom != NULL ? real_recvfrom(sockfd, buf, len, flags, src_addr, addrlen) : -1;
  if (result > 0) {
    record_packet(TGDB_PKT_IN, (const struct sockaddr*)src_addr,
                  addrlen != NULL ? *addrlen : 0, buf, (size_t)result, 0);
  }
  return result;
}

ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr,
               socklen_t addrlen) {
  if (real_sendto == NULL) {
    real_sendto = (sendto_fn)dlsym(RTLD_NEXT, "sendto");
  }
  const ssize_t result =
      real_sendto != NULL ? real_sendto(sockfd, buf, len, flags, dest_addr, addrlen) : -1;
  if (result > 0) {
    record_packet(TGDB_PKT_OUT, dest_addr, addrlen, buf, (size_t)result, 0);
  }
  return result;
}

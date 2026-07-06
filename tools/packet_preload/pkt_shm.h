#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TGDB_PKT_MAGIC 0x54474442u /* 'TGDB' little-endian */
#define TGDB_PKT_VERSION 1u
#define TGDB_PKT_CAPACITY 256u
#define TGDB_PKT_MAX_PAYLOAD 1500u

enum TgdbPktDirection {
  TGDB_PKT_IN = 0,
  TGDB_PKT_OUT = 1,
};

typedef struct TgdbPktEntry {
  uint64_t timestamp_ns;
  uint8_t direction;
  uint8_t reserved0;
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t src_ipv4;
  uint32_t dst_ipv4;
  uint16_t payload_len;
  uint16_t reserved1;
  uint8_t payload[TGDB_PKT_MAX_PAYLOAD];
} TgdbPktEntry;

typedef struct TgdbPktHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t capacity;
  uint32_t entry_stride;
  uint32_t pid;
  uint32_t reserved;
  uint32_t write_idx;
} TgdbPktHeader;

static inline TgdbPktEntry* tgdb_pkt_entries(TgdbPktHeader* header) {
  return (TgdbPktEntry*)(header + 1);
}

static inline uint32_t tgdb_pkt_slot_index(uint32_t write_idx, uint32_t capacity) {
  return write_idx % capacity;
}

#ifdef __cplusplus
}
#endif

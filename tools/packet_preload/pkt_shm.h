#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TUIDE_PKT_MAGIC 0x44495554u /* 'TUID' little-endian */
#define TUIDE_PKT_VERSION 1u
#define TUIDE_PKT_CAPACITY 256u
#define TUIDE_PKT_MAX_PAYLOAD 1500u

enum TuidePktDirection {
  TUIDE_PKT_IN = 0,
  TUIDE_PKT_OUT = 1,
};

typedef struct TuidePktEntry {
  uint64_t timestamp_ns;
  uint8_t direction;
  uint8_t reserved0;
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t src_ipv4;
  uint32_t dst_ipv4;
  uint16_t payload_len;
  uint16_t reserved1;
  uint8_t payload[TUIDE_PKT_MAX_PAYLOAD];
} TuidePktEntry;

typedef struct TuidePktHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t capacity;
  uint32_t entry_stride;
  uint32_t pid;
  uint32_t reserved;
  uint32_t write_idx;
} TuidePktHeader;

static inline TuidePktEntry* tuide_pkt_entries(TuidePktHeader* header) {
  return (TuidePktEntry*)(header + 1);
}

static inline uint32_t tuide_pkt_slot_index(uint32_t write_idx, uint32_t capacity) {
  return write_idx % capacity;
}

#ifdef __cplusplus
}
#endif

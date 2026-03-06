#pragma once

#include <stdint.h>
#include <string.h>

namespace uguisu {

static constexpr uint8_t PROTOCOL_VERSION = 1;

static constexpr size_t MIC_LEN = 4;
static constexpr size_t MSG_LEN = 7;       // device_id(2) + counter(4) + command(1)
static constexpr size_t PAYLOAD_LEN = 11;  // msg(7) + mic(4)
static constexpr size_t NONCE_LEN = 13;    // le16(device_id) + le32(counter) + zeros(7)

enum class Command : uint8_t {
  Unlock = 0x01,
  Lock = 0x02,
};

struct Payload {
  uint16_t device_id;
  uint32_t counter;
  Command command;
  uint8_t mic[MIC_LEN];
};

inline void le16_write(uint8_t out[2], uint16_t x) {
  out[0] = static_cast<uint8_t>(x & 0xFF);
  out[1] = static_cast<uint8_t>((x >> 8) & 0xFF);
}

inline void le32_write(uint8_t out[4], uint32_t x) {
  out[0] = static_cast<uint8_t>(x & 0xFF);
  out[1] = static_cast<uint8_t>((x >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((x >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((x >> 24) & 0xFF);
}

inline void build_nonce(uint16_t device_id, uint32_t counter, uint8_t nonce[NONCE_LEN]) {
  le16_write(&nonce[0], device_id);
  le32_write(&nonce[2], counter);
  for (size_t i = 6; i < NONCE_LEN; i++) nonce[i] = 0;
}

inline void build_msg(uint16_t device_id, uint32_t counter, Command command, uint8_t msg[MSG_LEN]) {
  le16_write(&msg[0], device_id);
  le32_write(&msg[2], counter);
  msg[6] = static_cast<uint8_t>(command);
}

// Computes AES-CCM tag (MIC) with:
// - nonce: le16(device_id) + le32(counter) + zeros(7)
// - message: le16(device_id) + le32(counter) + command (7 bytes)
// - tag length: 4 bytes
//
// The implementation matches Guillemot's receiver-side MIC generation.
bool ccm_mic_4(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* msg, size_t msg_len, uint8_t out_mic[MIC_LEN]);

}  // namespace uguisu


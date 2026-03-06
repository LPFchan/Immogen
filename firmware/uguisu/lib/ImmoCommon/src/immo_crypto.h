#pragma once
#include <stdint.h>
#include <stddef.h>

namespace immo {

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

void build_nonce(uint16_t device_id, uint32_t counter, uint8_t nonce[NONCE_LEN]);
void build_msg(uint16_t device_id, uint32_t counter, Command command, uint8_t msg[MSG_LEN]);

bool ccm_mic_4(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* msg, size_t msg_len, uint8_t out_mic[MIC_LEN]);

bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n);

}  // namespace immo

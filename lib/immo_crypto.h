#pragma once
#include <stdint.h>
#include <stddef.h>

namespace immo {

static constexpr size_t MIC_LEN = 8;
static constexpr size_t MSG_LEN = 5;       // counter(4) + command(1)
static constexpr size_t PAYLOAD_LEN = 13;  // msg(5) + mic(8)
static constexpr size_t NONCE_LEN = 13;    // le32(counter) + zeros(9)

enum class Command : uint8_t {
  Unlock = 0x01,
  Lock = 0x02,
};

struct Payload {
  uint32_t counter;
  Command command;
  uint8_t mic[MIC_LEN];
};

void build_nonce(uint32_t counter, uint8_t nonce[NONCE_LEN]);
void build_msg(uint32_t counter, Command command, uint8_t msg[MSG_LEN]);

bool ccm_mic_8(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* msg, size_t msg_len, uint8_t out_mic[MIC_LEN]);

bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n);

}  // namespace immo

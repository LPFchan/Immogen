#pragma once
#include <stdint.h>
#include <stddef.h>

namespace immo {

static constexpr size_t MIC_LEN = 8;
static constexpr size_t MSG_LEN = 6;       // prefix(1) + counter(4) + command(1)
static constexpr size_t PAYLOAD_LEN = 14;  // msg(6) + mic(8)
static constexpr size_t NONCE_LEN = 13;    // le32(counter) + zeros(9)

static constexpr uint16_t IMMOGEN_MAGIC = 0x494D; // "IM"

enum class Command : uint8_t {
  Unlock = 0x01,
  Lock = 0x02,
  Identify = 0x03,
  Window = 0x04,
};

struct Payload {
  uint8_t prefix;
  uint32_t counter;
  Command command;
  uint8_t mic[MIC_LEN];
};

void build_nonce(uint32_t counter, uint8_t nonce[NONCE_LEN]);
void build_msg(uint8_t prefix, uint32_t counter, Command command, uint8_t msg[MSG_LEN]);

bool ccm_auth_encrypt(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* msg, size_t msg_len, size_t aad_len, uint8_t* out_ct, uint8_t out_mic[MIC_LEN]);
bool ccm_auth_decrypt(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* ct, size_t ct_len, size_t aad_len, uint8_t* out_msg, uint8_t out_mic[MIC_LEN]);

bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n);

struct KeySlot; // Forward declaration
bool verify_payload(const uint8_t ct[MSG_LEN], const uint8_t mic[MIC_LEN], const KeySlot slots[4], Payload& out_pl, uint8_t& out_slot_id);

}  // namespace immo

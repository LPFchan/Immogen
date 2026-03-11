#include "immo_crypto.h"
#include "immo_storage.h"
#include <nrf_soc.h>
#include <string.h>

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

namespace immo {
namespace {

void le32_write(uint8_t out[4], uint32_t x) {
  out[0] = static_cast<uint8_t>(x & 0xFF);
  out[1] = static_cast<uint8_t>((x >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((x >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((x >> 24) & 0xFF);
}

bool aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  nrf_ecb_hal_data_t ecb{};
  memcpy(ecb.key, key, 16);
  memcpy(ecb.cleartext, in, 16);
  const uint32_t err = sd_ecb_block_encrypt(&ecb);
  if (err != NRF_SUCCESS) return false;
  memcpy(out, ecb.ciphertext, 16);
  return true;
}

void xor_block(uint8_t dst[16], const uint8_t a[16], const uint8_t b[16]) {
  for (size_t i = 0; i < 16; i++) dst[i] = a[i] ^ b[i];
}

}  // namespace

void build_nonce(uint32_t counter, uint8_t nonce[NONCE_LEN]) {
  le32_write(&nonce[0], counter);
  for (size_t i = 4; i < NONCE_LEN; i++) nonce[i] = 0;
}

void build_msg(uint8_t prefix, uint32_t counter, Command command, uint8_t msg[MSG_LEN]) {
  msg[0] = prefix;
  le32_write(&msg[1], counter);
  msg[5] = static_cast<uint8_t>(command);
}

bool ccm_auth_encrypt(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* msg, size_t msg_len, size_t aad_len, uint8_t* out_ct, uint8_t out_mic[MIC_LEN]) {
  if (msg_len > 0xFFFFu || aad_len > msg_len) return false;

  const size_t payload_len = msg_len - aad_len;
  const uint8_t L = 2;
  const uint8_t M = MIC_LEN;
  
  // RFC 3610: B0 = Flags | Nonce | PayloadLength
  // Flags: bit 6 is Adata (1 if AAD present), bits 5-3 are (M-2)/2, bits 2-0 are L-1
  const uint8_t flags_b0 = static_cast<uint8_t>((aad_len > 0 ? 0x40 : 0) | (((M - 2) / 2) << 3) | (L - 1));

  uint8_t b0[16]{};
  b0[0] = flags_b0;
  memcpy(&b0[1], nonce, NONCE_LEN);
  b0[14] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
  b0[15] = static_cast<uint8_t>(payload_len & 0xFF);

  uint8_t x[16]{};
  uint8_t tmp[16]{};
  xor_block(tmp, x, b0);
  if (!aes128_ecb_encrypt(key, tmp, x)) return false;

  // If AAD present, process AAD blocks
  if (aad_len > 0) {
    uint8_t block[16]{};
    // RFC 3610: AAD blocks start with 2-byte or 6-byte length header.
    // Here we assume aad_len < 0xFF00, so 2-byte header.
    block[0] = static_cast<uint8_t>((aad_len >> 8) & 0xFF);
    block[1] = static_cast<uint8_t>(aad_len & 0xFF);
    
    size_t aad_idx = 0;
    size_t block_idx = 2;
    while (aad_idx < aad_len) {
      const size_t n = min(static_cast<size_t>(16 - block_idx), aad_len - aad_idx);
      memcpy(&block[block_idx], &msg[aad_idx], n);
      aad_idx += n;
      block_idx += n;
      
      if (block_idx == 16 || aad_idx == aad_len) {
        xor_block(tmp, x, block);
        if (!aes128_ecb_encrypt(key, tmp, x)) return false;
        memset(block, 0, 16);
        block_idx = 0;
      }
    }
  }

  // Process payload blocks for authentication
  size_t payload_idx = 0;
  while (payload_idx < payload_len) {
    uint8_t block[16]{};
    const size_t n = min(static_cast<size_t>(16), payload_len - payload_idx);
    memcpy(block, &msg[aad_len + payload_idx], n);
    xor_block(tmp, x, block);
    if (!aes128_ecb_encrypt(key, tmp, x)) return false;
    payload_idx += n;
  }

  // Generate MIC (S0 encryption of final X)
  uint8_t a0[16]{};
  a0[0] = static_cast<uint8_t>(L - 1);
  memcpy(&a0[1], nonce, NONCE_LEN);
  // a0[14, 15] are 0 for S0
  
  uint8_t s0[16]{};
  if (!aes128_ecb_encrypt(key, a0, s0)) return false;
  for (size_t i = 0; i < M; i++) out_mic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);

  // Encryption of payload
  for (size_t i = 0; i < aad_len; i++) out_ct[i] = msg[i];

  size_t offset_enc = 0;
  for (uint16_t ctr_i = 1; offset_enc < payload_len; ctr_i++) {
    uint8_t ai[16]{};
    ai[0] = static_cast<uint8_t>(L - 1);
    memcpy(&ai[1], nonce, NONCE_LEN);
    ai[14] = static_cast<uint8_t>((ctr_i >> 8) & 0xFF);
    ai[15] = static_cast<uint8_t>(ctr_i & 0xFF);

    uint8_t si[16]{};
    if (!aes128_ecb_encrypt(key, ai, si)) return false;

    const size_t n = min(static_cast<size_t>(16), payload_len - offset_enc);
    for (size_t j = 0; j < n; j++) {
      out_ct[aad_len + offset_enc + j] = msg[aad_len + offset_enc + j] ^ si[j];
    }
    offset_enc += n;
  }

  return true;
}

bool ccm_auth_decrypt(const uint8_t key[16], const uint8_t nonce[NONCE_LEN], const uint8_t* ct, size_t ct_len, size_t aad_len, uint8_t* out_msg, uint8_t out_mic[MIC_LEN]) {
  if (ct_len > 0xFFFFu || aad_len > ct_len) return false;

  const size_t payload_len = ct_len - aad_len;
  const uint8_t L = 2;
  const uint8_t M = MIC_LEN;

  // Copy AAD to out_msg
  for (size_t i = 0; i < aad_len; i++) out_msg[i] = ct[i];

  // Decrypt payload
  size_t offset_enc = 0;
  for (uint16_t ctr_i = 1; offset_enc < payload_len; ctr_i++) {
    uint8_t ai[16]{};
    ai[0] = static_cast<uint8_t>(L - 1);
    memcpy(&ai[1], nonce, NONCE_LEN);
    ai[14] = static_cast<uint8_t>((ctr_i >> 8) & 0xFF);
    ai[15] = static_cast<uint8_t>(ctr_i & 0xFF);

    uint8_t si[16]{};
    if (!aes128_ecb_encrypt(key, ai, si)) return false;

    const size_t n = min(static_cast<size_t>(16), payload_len - offset_enc);
    for (size_t j = 0; j < n; j++) {
      out_msg[aad_len + offset_enc + j] = ct[aad_len + offset_enc + j] ^ si[j];
    }
    offset_enc += n;
  }

  // Authentication: re-compute MIC
  const uint8_t flags_b0 = static_cast<uint8_t>((aad_len > 0 ? 0x40 : 0) | (((M - 2) / 2) << 3) | (L - 1));
  uint8_t b0[16]{};
  b0[0] = flags_b0;
  memcpy(&b0[1], nonce, NONCE_LEN);
  b0[14] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
  b0[15] = static_cast<uint8_t>(payload_len & 0xFF);

  uint8_t x[16]{};
  uint8_t tmp[16]{};
  xor_block(tmp, x, b0);
  if (!aes128_ecb_encrypt(key, tmp, x)) return false;

  if (aad_len > 0) {
    uint8_t block[16]{};
    block[0] = static_cast<uint8_t>((aad_len >> 8) & 0xFF);
    block[1] = static_cast<uint8_t>(aad_len & 0xFF);
    
    size_t aad_idx = 0;
    size_t block_idx = 2;
    while (aad_idx < aad_len) {
      const size_t n = min(static_cast<size_t>(16 - block_idx), aad_len - aad_idx);
      memcpy(&block[block_idx], &out_msg[aad_idx], n);
      aad_idx += n;
      block_idx += n;
      
      if (block_idx == 16 || aad_idx == aad_len) {
        xor_block(tmp, x, block);
        if (!aes128_ecb_encrypt(key, tmp, x)) return false;
        memset(block, 0, 16);
        block_idx = 0;
      }
    }
  }

  size_t payload_idx = 0;
  while (payload_idx < payload_len) {
    uint8_t block[16]{};
    const size_t n = min(static_cast<size_t>(16), payload_len - payload_idx);
    memcpy(block, &out_msg[aad_len + payload_idx], n);
    xor_block(tmp, x, block);
    if (!aes128_ecb_encrypt(key, tmp, x)) return false;
    payload_idx += n;
  }

  uint8_t a0[16]{};
  a0[0] = static_cast<uint8_t>(L - 1);
  memcpy(&a0[1], nonce, NONCE_LEN);
  
  uint8_t s0[16]{};
  if (!aes128_ecb_encrypt(key, a0, s0)) return false;
  for (size_t i = 0; i < M; i++) out_mic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);

  return true;
}

bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (a[i] ^ b[i]);
  return diff == 0;
}

bool verify_payload(const uint8_t ct[MSG_LEN], const uint8_t mic[MIC_LEN], const KeySlot slots[MAX_KEY_SLOTS], Payload& out_pl, uint8_t& out_slot_id) {
  const uint8_t prefix = ct[0];
  const uint8_t slot_id = (prefix >> 4) & 0x03;
  out_slot_id = slot_id;

  const KeySlot& slot = slots[slot_id];
  bool is_active = false;
  for (int i = 0; i < 16; i++) {
    if (slot.aes_key[i] != 0) {
      is_active = true;
      break;
    }
  }
  if (!is_active) return false;

  const uint32_t counter = static_cast<uint32_t>(ct[1] | (static_cast<uint32_t>(ct[2]) << 8) | (static_cast<uint32_t>(ct[3]) << 16) | (static_cast<uint32_t>(ct[4]) << 24));

  uint8_t nonce[NONCE_LEN];
  build_nonce(counter, nonce);

  uint8_t msg[MSG_LEN];
  uint8_t expected[MIC_LEN];
  
  if (!ccm_auth_decrypt(slot.aes_key, nonce, ct, MSG_LEN, 5, msg, expected)) return false;

  if (!constant_time_eq(expected, mic, MIC_LEN)) return false;

  out_pl.prefix = prefix;
  out_pl.counter = counter;
  out_pl.command = static_cast<Command>(msg[5]);
  memcpy(out_pl.mic, mic, MIC_LEN);
  return true;
}

}  // namespace immo

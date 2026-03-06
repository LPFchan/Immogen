#pragma once

// Example pre-shared 128-bit key used for MIC verification (AES-128-CCM).
// For real use, create `guillemot_secrets_local.h` (gitignored) and define
// `GUILLEMOT_PSK_BYTES` there.
//
// Example:
//   #define GUILLEMOT_PSK_BYTES 0x01,0x02,... (16 bytes total)
//
#define GUILLEMOT_PSK_BYTES  \
  0x00, 0x00, 0x00, 0x00,    \
  0x00, 0x00, 0x00, 0x00,    \
  0x00, 0x00, 0x00, 0x00,    \
  0x00, 0x00, 0x00, 0x00


#pragma once

// Example pre-shared 128-bit key used for MIC generation (AES-CCM).
// For real use, create `uguisu_secrets_local.h` (gitignored) and define
// `UGUISU_PSK_BYTES` there.
//
// Example:
//   #define UGUISU_PSK_BYTES 0x01,0x02,... (16 bytes total)
//
#define UGUISU_PSK_BYTES  \
  0x00, 0x00, 0x00, 0x00,  \
  0x00, 0x00, 0x00, 0x00,  \
  0x00, 0x00, 0x00, 0x00,  \
  0x00, 0x00, 0x00, 0x00


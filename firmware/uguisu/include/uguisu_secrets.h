#pragma once

#if __has_include("uguisu_secrets_local.h")
#include "uguisu_secrets_local.h"
#endif

#include "uguisu_secrets.example.h"

static constexpr uint8_t UGUISU_PSK[16] = {UGUISU_PSK_BYTES};


#pragma once

#if __has_include("guillemot_secrets_local.h")
#include "guillemot_secrets_local.h"
#endif

#include "guillemot_secrets.example.h"

static constexpr uint8_t GUILLEMOT_PSK[16] = {GUILLEMOT_PSK_BYTES};


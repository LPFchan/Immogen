#ifndef IMMO_TUSB_CONFIG_H
#define IMMO_TUSB_CONFIG_H

// Include the default Adafruit nRF configuration
#include "arduino/ports/nrf/tusb_config_nrf.h"

// Forcefully disable Mass Storage Class for both Device and Host
// to prevent Adafruit_TinyUSB from including SdFat and causing
// a `File` class collision with Adafruit_LittleFS.

#undef CFG_TUD_MSC
#define CFG_TUD_MSC 0

#undef CFG_TUH_MSC
#define CFG_TUH_MSC 0

#endif // IMMO_TUSB_CONFIG_H

/*
 * Wrapper for Adafruit TinyUSB to avoid pulling in SdFat (and its "File" typedef)
 * when building framework Bluefruit52Lib bonding.cpp, which uses LittleFS File.
 * When BONDING_CPP_BUILD is defined, we skip the Host MSC include that pulls SdFat.
 * Original: Adafruit TinyUSB Library (MIT).
 */
#ifndef ADAFRUIT_TINYUSB_H_
#define ADAFRUIT_TINYUSB_H_

#include "tusb_option.h"

// Device
#if CFG_TUD_ENABLED

#include "arduino/Adafruit_USBD_Device.h"

#if CFG_TUD_CDC
#include "arduino/Adafruit_USBD_CDC.h"
#endif

#if CFG_TUD_HID
#include "arduino/hid/Adafruit_USBD_HID.h"
#endif

#if CFG_TUD_MIDI
#include "arduino/midi/Adafruit_USBD_MIDI.h"
#endif

#if CFG_TUD_MSC
#include "arduino/msc/Adafruit_USBD_MSC.h"
#endif

#if CFG_TUD_VENDOR
#include "arduino/webusb/Adafruit_USBD_WebUSB.h"
#endif

#if CFG_TUD_VIDEO
#include "arduino/video/Adafruit_USBD_Video.h"
#endif

// Initialize device hardware, stack, also Serial as CDC
void TinyUSB_Device_Init(uint8_t rhport);

#endif

// Host
#if CFG_TUH_ENABLED

#include "arduino/Adafruit_USBH_Host.h"

#if CFG_TUH_CDC
#include "arduino/cdc/Adafruit_USBH_CDC.h"
#endif

/* Skip Host MSC when building bonding.cpp to avoid SdFat's File vs LittleFS File ambiguity */
#if CFG_TUH_MSC && !defined(BONDING_CPP_BUILD)
#include "arduino/msc/Adafruit_USBH_MSC.h"
#endif

#endif

#endif /* ADAFRUIT_TINYUSB_H_ */

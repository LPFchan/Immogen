# Uguisu Maintainer Guide: Resolving the `File` Naming Collision

## Background

Both the `Adafruit_LittleFS` library and `SdFat` library (which is included automatically by the `Adafruit TinyUSB Library` for Mass Storage support) declare a `File` class in the global C++ namespace. When building a project that uses LittleFS on the Adafruit nRF52 core with TinyUSB, this causes a fatal naming collision.

Historically, this issue was sidestepped by using a fragile Python script (`extra_script.py`) to intercept the framework compilation and inject a custom header to hide `SdFat`. 

However, this method is brittle and prone to breaking when PlatformIO or the underlying framework updates.

## The Solution

A much cleaner, native solution is to use TinyUSB's built-in configuration macros to completely disable Mass Storage Class (MSC) support, which prevents the framework from including `SdFat` in the first place.

This fix has now been integrated directly into the shared `ImmoCommon` library.

## How to Update Uguisu

To apply this cleaner fix to the Uguisu fob project, you need to update its `platformio.ini` to consume the new shared configuration file.

### 1. Ensure `ImmoCommon` is Updated
First, make sure the `ImmoCommon` submodule in your Uguisu repository is pulled to the latest commit containing `src/immo_tusb_config.h`.

### 2. Update `platformio.ini`
In the Uguisu `platformio.ini` file, perform the following modifications:

1. **Remove the extra script hack:**
   Delete the line that says `extra_scripts = pre:extra_script.py`. You can also safely delete the `extra_script.py` file and the `include_fix` directory from the repository.

2. **Add the SPI library dependency:**
   Because TinyUSB was implicitly resolving the `SPI.h` dependency for the build graph, disabling MSC can cause PlatformIO's Library Dependency Finder (LDF) to miss it. Add `SPI` explicitly to `lib_deps`.

3. **Configure LDF to search deeper:**
   Add `lib_ldf_mode = deep+` to ensure PlatformIO correctly parses dependencies inside the modified framework macros.

4. **Add the Build Flags:**
   Inject the new configuration file via `-D CFG_TUSB_CONFIG_FILE`. You must also explicitly add the `ImmoCommon/src` directory to the global include path so that the TinyUSB library can find it during compilation.

### Example `platformio.ini` Changes:

```ini
; ... existing config ...

lib_deps =
  afantor/Bluefruit52_Arduino
  adafruit/Adafruit TinyUSB Library
  SPI            ; <-- Add this

lib_ldf_mode = deep+   ; <-- Add this

build_flags =
  ; ... other flags ...
  -D CFG_TUSB_CONFIG_FILE=\"immo_tusb_config.h\"   ; <-- Add this
  -I ../../lib                            ; <-- Add this
  -I ".pio/libdeps/your_board_name/Adafruit TinyUSB Library/src"
```

Once these changes are made, the project will compile cleanly without the need for brittle python build scripts, and the `File` class will unambiguously resolve to `Adafruit_LittleFS`.

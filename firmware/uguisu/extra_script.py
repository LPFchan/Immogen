"""Add framework libraries include path so Wire/SPI etc. resolve when TinyUSB is included.
   When building framework Bluefruit52Lib bonding.cpp, use a TinyUSB wrapper that skips
   Host MSC (SdFat) so File stays unambiguous (LittleFS only)."""
import os

Import("env")

# Framework package (e.g. framework-arduinoadafruitnrf52-seeed) lives in PlatformIO packages.
try:
    core_dir = env.get("PROJECT_CORE_DIR") or os.path.expanduser("~/.platformio")
    pkg_dir = os.path.join(core_dir, "packages")
    if os.path.isdir(pkg_dir):
        for name in os.listdir(pkg_dir):
            if name.startswith("framework-arduinoadafruitnrf52"):
                libs = os.path.join(pkg_dir, name, "libraries")
                if os.path.isdir(libs):
                    env.Append(BUILD_FLAGS=["-I" + libs])
                    env.Append(CPPPATH=[libs])
                    spi_dir = os.path.join(libs, "SPI")
                    if os.path.isdir(spi_dir):
                        env.Append(BUILD_FLAGS=["-I" + spi_dir])
                        env.Append(CPPPATH=[spi_dir])
                    break
except Exception:
    pass

# When compiling framework Bluefruit52Lib bonding.cpp, use our Adafruit_TinyUSB.h wrapper.
try:
    project_dir = env.get("PROJECT_DIR", "")
    include_fix = os.path.join(project_dir, "include_fix")
    if not os.path.isdir(include_fix):
        include_fix = ""
except Exception:
    include_fix = ""

if include_fix:
    def _bonding_emitter(target, source, emit_env):
        if source and len(source) > 0:
            src = str(source[0]).replace("\\", "/")
            if "Bluefruit52Lib" in src and "bonding.cpp" in src:
                emit_env.Append(CXXFLAGS=["-DBONDING_CPP_BUILD"])
                emit_env.Prepend(CPPPATH=[include_fix])
        return target, source

    try:
        obj_builder = env.get("BUILDERS", {}).get("Object")
        if obj_builder and hasattr(obj_builder, "add_emitter"):
            obj_builder.add_emitter(".cpp", _bonding_emitter)
    except Exception:
        pass

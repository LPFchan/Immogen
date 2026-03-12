#include "immo_storage.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

namespace immo {
namespace {

uint32_t crc32_ieee(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

uint32_t record_crc(uint8_t slot_id, uint32_t counter) {
  uint8_t data[5];
  data[0] = slot_id;
  data[1] = static_cast<uint8_t>(counter & 0xFF);
  data[2] = static_cast<uint8_t>((counter >> 8) & 0xFF);
  data[3] = static_cast<uint8_t>((counter >> 16) & 0xFF);
  data[4] = static_cast<uint8_t>((counter >> 24) & 0xFF);
  return crc32_ieee(data, sizeof(data));
}

}  // namespace

CounterStore::CounterStore(const char* log_path, const char* old_log_path, size_t max_bytes)
  : log_path_(log_path), old_log_path_(old_log_path), max_bytes_(max_bytes) {
    for (int i = 0; i < MAX_KEY_SLOTS; i++) {
      last_counters_[i] = 0;
    }
}

bool CounterStore::begin() {
  return InternalFS.begin();
}

void CounterStore::load() {
  for (int i = 0; i < MAX_KEY_SLOTS; i++) {
    last_counters_[i] = 0;
  }
  scan_file_(log_path_);
  scan_file_(old_log_path_);
}

void CounterStore::scan_file_(const char* path) {
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_READ));
  if (!f) return;

  CounterRecord rec{};
  while (f.read(reinterpret_cast<void*>(&rec), sizeof(rec)) == sizeof(rec)) {
    if (rec.slot_id >= MAX_KEY_SLOTS) continue;
    if (record_crc(rec.slot_id, rec.counter) != rec.crc32) continue;
    if (rec.counter > last_counters_[rec.slot_id]) last_counters_[rec.slot_id] = rec.counter;
  }
}

uint32_t CounterStore::lastCounter(uint8_t slot_id) const {
  if (slot_id >= MAX_KEY_SLOTS) return 0;
  return last_counters_[slot_id];
}

void CounterStore::update(uint8_t slot_id, uint32_t counter) {
  if (slot_id >= MAX_KEY_SLOTS) return;
  rotateIfNeeded_();

  CounterRecord rec{};
  rec.slot_id = slot_id;
  rec.counter = counter;
  rec.crc32 = record_crc(slot_id, counter);

  // FILE_O_WRITE in Adafruit LittleFS seeks to end (append), enabling power-loss recovery.
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(log_path_, Adafruit_LittleFS_Namespace::FILE_O_WRITE));
  if (!f) return;
  f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  f.flush();

  last_counters_[slot_id] = counter;
}

void CounterStore::seed(uint8_t slot_id, uint32_t counter) {
  if (slot_id >= MAX_KEY_SLOTS) return;
  InternalFS.remove(log_path_);
  InternalFS.remove(old_log_path_);
  for (int i = 0; i < MAX_KEY_SLOTS; i++) {
    last_counters_[i] = 0;
  }
  update(slot_id, counter);
}

void CounterStore::rotateIfNeeded_() {
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(log_path_, Adafruit_LittleFS_Namespace::FILE_O_READ));
  if (!f) return;
  const size_t sz = f.size();
  f.close();

  if (sz < max_bytes_) return;
  InternalFS.remove(old_log_path_);
  InternalFS.rename(log_path_, old_log_path_);
}

}  // namespace immo

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

uint32_t record_crc(const CounterRecord& r) {
  uint8_t buf[sizeof(r.device_id) + sizeof(r.counter)];
  memcpy(buf + 0, &r.device_id, sizeof(r.device_id));
  memcpy(buf + sizeof(r.device_id), &r.counter, sizeof(r.counter));
  return crc32_ieee(buf, sizeof(buf));
}

}  // namespace

CounterStore::CounterStore(const char* log_path, const char* old_log_path, size_t max_bytes)
  : log_path_(log_path), old_log_path_(old_log_path), max_bytes_(max_bytes),
    last_device_id_(0), last_counter_(0) {}

bool CounterStore::begin() {
  return InternalFS.begin();
}

void CounterStore::load() {
  last_device_id_ = 0;
  last_counter_ = 0;

  Adafruit_LittleFS_Namespace::File f(InternalFS.open(log_path_, Adafruit_LittleFS_Namespace::FILE_O_READ));
  if (!f) return;

  CounterRecord rec{};
  while (f.read(reinterpret_cast<void*>(&rec), sizeof(rec)) == sizeof(rec)) {
    if (record_crc(rec) != rec.crc32) continue;
    last_device_id_ = rec.device_id;
    last_counter_ = rec.counter;
  }
}

uint32_t CounterStore::lastCounterFor(uint16_t device_id) const {
  if (device_id != last_device_id_) return 0;
  return last_counter_;
}

void CounterStore::update(uint16_t device_id, uint32_t counter) {
  rotateIfNeeded_();

  CounterRecord rec{};
  rec.device_id = device_id;
  rec.counter = counter;
  rec.crc32 = record_crc(rec);

  Adafruit_LittleFS_Namespace::File f(InternalFS.open(log_path_, Adafruit_LittleFS_Namespace::FILE_O_WRITE));
  if (!f) return;
  f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  f.flush();

  last_device_id_ = device_id;
  last_counter_ = counter;
}

void CounterStore::seed(uint16_t device_id, uint32_t counter) {
  InternalFS.remove(log_path_);
  InternalFS.remove(old_log_path_);
  update(device_id, counter);
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

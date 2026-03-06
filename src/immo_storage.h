#pragma once
#include <stdint.h>
#include <stddef.h>

namespace immo {

struct CounterRecord {
  uint16_t device_id;
  uint32_t counter;
  uint32_t crc32;
};

class CounterStore {
public:
  CounterStore(const char* log_path, const char* old_log_path, size_t max_bytes);

  bool begin();
  
  // Scans the log and populates internal state
  void load();

  // Returns the last counter for the given device ID. Returns 0 if never seen.
  uint32_t lastCounterFor(uint16_t device_id) const;

  // For devices that only have one device_id in the log (like Uguisu).
  uint32_t loadLast() {
    load();
    return last_counter_;
  }

  // Appends a new counter record to the log
  void update(uint16_t device_id, uint32_t counter);

  // Appends without specifying device id (defaults to internal last_device_id_)
  void append(uint32_t counter) {
    update(last_device_id_, counter);
  }

  // Replaces the entire log with a single record (useful during provisioning)
  void seed(uint16_t device_id, uint32_t counter);

private:
  void rotateIfNeeded_();

  const char* log_path_;
  const char* old_log_path_;
  size_t max_bytes_;

  uint16_t last_device_id_;
  uint32_t last_counter_;
};

}  // namespace immo

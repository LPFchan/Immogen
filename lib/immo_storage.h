#pragma once
#include <stdint.h>
#include <stddef.h>

namespace immo {

static constexpr size_t DEFAULT_COUNTER_LOG_MAX_BYTES = 4096;
static constexpr uint8_t MAX_KEY_SLOTS = 4;

struct KeySlot {
  uint8_t aes_key[16];
  uint32_t counter;
  char name[24];
};

struct CounterRecord {
  uint8_t slot_id;
  uint32_t counter;
  uint32_t crc32;
};

class CounterStore {
public:
  CounterStore(const char* log_path, const char* old_log_path, size_t max_bytes);

  bool begin();
  
  // Scans the log and populates internal state for all slots
  void load();

  // Returns the last counter seen for the given slot. Returns 0 if never seen.
  uint32_t lastCounter(uint8_t slot_id) const;

  // Appends a new counter record to the log
  void update(uint8_t slot_id, uint32_t counter);

  // Replaces the entire log with a single record (useful during provisioning)
  void seed(uint8_t slot_id, uint32_t counter);

private:
  void rotateIfNeeded_();
  void scan_file_(const char* path);

  const char* log_path_;
  const char* old_log_path_;
  size_t max_bytes_;

  uint32_t last_counters_[MAX_KEY_SLOTS];
};

}  // namespace immo

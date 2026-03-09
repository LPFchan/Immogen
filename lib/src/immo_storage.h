#pragma once
#include <stdint.h>
#include <stddef.h>

namespace immo {

static constexpr size_t DEFAULT_COUNTER_LOG_MAX_BYTES = 4096;

struct CounterRecord {
  uint32_t counter;
  uint32_t crc32;
};

class CounterStore {
public:
  CounterStore(const char* log_path, const char* old_log_path, size_t max_bytes);

  bool begin();
  
  // Scans the log and populates internal state
  void load();

  // Returns the last counter seen. Returns 0 if never seen.
  uint32_t lastCounter() const;

  // For devices that need to load and return last counter (e.g. Uguisu).
  uint32_t loadLast() {
    load();
    return last_counter_;
  }

  // Appends a new counter record to the log
  void update(uint32_t counter);

  // Replaces the entire log with a single record (useful during provisioning)
  void seed(uint32_t counter);

private:
  void rotateIfNeeded_();

  const char* log_path_;
  const char* old_log_path_;
  size_t max_bytes_;

  uint32_t last_counter_;
};

}  // namespace immo

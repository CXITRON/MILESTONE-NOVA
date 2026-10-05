#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
inline std::map<std::string, std::map<std::string, std::vector<uint8_t>>> testNvs;
inline bool testNvsWriteFail = false;
// When non-zero, writes that would push the total stored bytes past this fail (a full partition).
inline size_t testNvsCapacity = 0;
inline size_t testNvsBytes() {
  size_t total = 0;
  for (const auto &space : testNvs)
    for (const auto &item : space.second) total += item.second.size();
  return total;
}
class Preferences {
  std::string space_;
public:
  bool begin(const char *name, bool = false) { space_ = name; return true; }
  void end() {}
  bool isKey(const char *key) { return !testNvs[space_][key].empty(); }
  bool remove(const char *key) { return testNvs[space_].erase(key) > 0; }
  bool clear() { testNvs[space_].clear(); return true; }
  size_t getBytesLength(const char *key) { return testNvs[space_][key].size(); }
  size_t getBytes(const char *key, void *out, size_t size) {
    const auto &value = testNvs[space_][key];
    size = std::min(size, value.size());
    if (size) memcpy(out, value.data(), size);
    return size;
  }
  size_t putBytes(const char *key, const void *data, size_t size) {
    if (testNvsWriteFail) return 0;
    if (testNvsCapacity &&
        testNvsBytes() - testNvs[space_][key].size() + size > testNvsCapacity) return 0;
    const auto *bytes = static_cast<const uint8_t *>(data);
    testNvs[space_][key] = {bytes, bytes + size};
    return size;
  }
  std::string getString(const char *key, const char *fallback = "") {
    const auto &value = testNvs[space_][key];
    return value.empty() ? fallback : std::string(value.begin(), value.end());
  }
  size_t getString(const char *key, char *out, size_t size) {
    const auto value = getString(key);
    if (!size) return 0;
    const auto count = std::min(size - 1, value.size());
    memcpy(out, value.data(), count);
    out[count] = 0;
    return count;
  }
  size_t putString(const char *key, const char *text) { return putBytes(key, text, strlen(text)); }
  bool getBool(const char *key, bool fallback = false) {
    const auto &value = testNvs[space_][key];
    return value.size() == 1 ? value[0] != 0 : fallback;
  }
  size_t putBool(const char *key, bool value) {
    const uint8_t byte = value;
    return putBytes(key, &byte, 1);
  }
};

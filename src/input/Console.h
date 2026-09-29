#pragma once
#include <cstddef>
namespace nova {
class Console {
public:
  const char *poll();

private:
  char line_[512]{};
  size_t used_ = 0;
  bool ready_ = false, overflow_ = false;
};
} // namespace nova

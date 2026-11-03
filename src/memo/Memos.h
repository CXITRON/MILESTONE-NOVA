#pragma once
#include "MemoNames.h"
#include "Morse.h"
#include <ctime>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace nova {
class Storage;
struct FileListing;
// Morse memos on SD as /memo/YYYYMMDD-HHMMSS.txt (UTF-8). App asks for one operation at a time
// and polls the result; the SD work runs on the portal/service worker, never on the UI loop.
class Memos {
public:
  enum class Op : uint8_t { None, List, Read, Save, Remove };
  bool begin(Storage &storage);
  bool list();
  bool read(const char *name);
  bool save(const char *text, time_t epoch);
  bool remove(const char *name);
  bool busy() const;
  // Takes a finished operation. The index or text it filled stays valid until the next request.
  bool receive(Op &op, bool &ok);
  const MemoIndex &index() const { return index_; }
  const char *text() const { return text_; }
  void process(); // Service worker only.

private:
  bool start(Op op, const char *name);
  bool doList();
  bool doRead();
  bool doSave();
  bool doRemove();
  Storage *storage_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  FileListing *listing_ = nullptr;
  Op pending_ = Op::None, finished_ = Op::None;
  bool ok_ = false;
  time_t epoch_ = 0;
  char name_[MemoIndex::nameSize]{};
  char text_[morse::Editor::capacity + 1]{};
  MemoIndex index_;
};
} // namespace nova

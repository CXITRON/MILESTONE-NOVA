#pragma once
#include "MemoNames.h"
#include "Morse.h"
#include <ctime>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace nova {
class Storage;
struct FileListing;
// Morse memos on SD as /memo/YYYYMMDD-HHMMSS.txt (UTF-8). Saving an
// opened memo replaces its file; a new memo never overwrites one. App asks for one operation at a time
// and polls the result; the SD work runs on the portal/service worker, never on the UI loop.
class Memos {
public:
  enum class Op : uint8_t { None, List, Read, Save, Remove };
  bool begin(Storage &storage);
  bool list();
  bool read(const char *name);
  // A new memo named from `epoch`, or, with `name`, that memo replaced (atomic write).
  bool save(const char *text, time_t epoch, const char *name = nullptr);
  bool remove(const char *name);
  bool busy() const;
  // Takes a finished operation. The index or text it filled stays valid until the next request.
  bool receive(Op &op, bool &ok);
  const MemoIndex &index() const { return index_; }
  const char *text() const { return text_; }
  // The last read held the whole file unchanged, so saving the edited text loses nothing.
  bool whole() const { return whole_; }
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
  bool ok_ = false, whole_ = false, replace_ = false;
  time_t epoch_ = 0;
  char name_[MemoIndex::nameSize]{};
  char text_[morse::Editor::capacity + 1]{};
  MemoIndex index_;
};
} // namespace nova

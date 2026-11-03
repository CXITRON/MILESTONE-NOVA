#include "Memos.h"
#include "../core/Text.h"
#include "../storage/Storage.h"
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>
namespace nova {
bool Memos::begin(Storage &storage) {
  storage_ = &storage;
  mutex_ = xSemaphoreCreateMutex();
  if (void *p = heap_caps_malloc(sizeof(FileListing), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
    listing_ = static_cast<FileListing *>(p);
  return mutex_ && listing_;
}
bool Memos::start(Op op, const char *name) {
  if (!mutex_ || !listing_)
    return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool idle = pending_ == Op::None && finished_ == Op::None;
  if (idle) {
    if (name)
      snprintf(name_, sizeof(name_), "%s", name);
    pending_ = op;
  }
  xSemaphoreGive(mutex_);
  return idle;
}
bool Memos::list() { return start(Op::List, nullptr); }
bool Memos::read(const char *name) { return start(Op::Read, name); }
bool Memos::remove(const char *name) { return start(Op::Remove, name); }
bool Memos::save(const char *text, time_t epoch) {
  if (busy())
    return false;
  // The worker reads text_ only after start() publishes the request below.
  snprintf(text_, sizeof(text_), "%s", text);
  epoch_ = epoch;
  return start(Op::Save, nullptr);
}
bool Memos::busy() const {
  if (!mutex_)
    return true;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool busy = pending_ != Op::None || finished_ != Op::None;
  xSemaphoreGive(mutex_);
  return busy;
}
bool Memos::receive(Op &op, bool &ok) {
  if (!mutex_)
    return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  op = finished_;
  ok = ok_;
  finished_ = Op::None;
  xSemaphoreGive(mutex_);
  return op != Op::None;
}
void Memos::process() {
  if (!mutex_)
    return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const Op op = pending_;
  xSemaphoreGive(mutex_);
  if (op == Op::None)
    return;
  const bool mounted = storage_->mounted();
  const bool ok = mounted && (op == Op::List   ? doList()
                              : op == Op::Read ? doRead()
                              : op == Op::Save ? doSave()
                                               : doRemove());
  xSemaphoreTake(mutex_, portMAX_DELAY);
  ok_ = ok;
  finished_ = op;
  pending_ = Op::None;
  xSemaphoreGive(mutex_);
}
bool Memos::doList() {
  index_.clear();
  FileJob job;
  job.op = FileOp::List;
  strcpy(job.path, "/memo");
  job.data = reinterpret_cast<uint8_t *>(listing_);
  job.length = sizeof(FileListing);
  // Storage lists 32 files per call in directory order; walk every page to find the newest.
  for (unsigned page = 0; page < 64; ++page) {
    job.offset = page * 32;
    if (!storage_->execute(job))
      return false;
    char name[MemoIndex::nameSize];
    for (unsigned i = 0; i < listing_->count; ++i)
      if (memoName(listing_->entries[i].path, name))
        index_.add(name);
    if (listing_->count < 32)
      return true;
  }
  return true;
}
bool Memos::doRead() {
  FileJob job;
  job.op = FileOp::Read;
  memoPath(name_, job.path, sizeof(job.path));
  job.data = reinterpret_cast<uint8_t *>(text_);
  job.length = sizeof(text_) - 1;
  text_[0] = 0;
  if (!storage_->execute(job))
    return false;
  text_[job.actual] = 0;
  // A file edited elsewhere may not be valid UTF-8; show what is safe.
  char *clean = reinterpret_cast<char *>(listing_);
  cleanUtf8(clean, sizeof(text_), text_);
  strcpy(text_, clean);
  return true;
}
bool Memos::doSave() {
  FileJob job;
  bool free = false;
  // Never overwrite: a second memo in the same second takes the next free second.
  for (unsigned bump = 0; bump < 10 && !free; ++bump) {
    memoNameAt(epoch_, bump, name_);
    job = FileJob{};
    job.op = FileOp::Info;
    memoPath(name_, job.path, sizeof(job.path));
    if (storage_->execute(job))
      continue; // Taken.
    if (job.error[0])
      return false; // Storage busy or path refused: not proof that the name is free.
    free = true;
  }
  if (!free)
    return false;
  job.op = FileOp::Write;
  job.data = reinterpret_cast<uint8_t *>(text_);
  job.length = strlen(text_);
  return storage_->execute(job);
}
bool Memos::doRemove() {
  FileJob job;
  job.op = FileOp::Remove;
  memoPath(name_, job.path, sizeof(job.path));
  return storage_->execute(job);
}
} // namespace nova

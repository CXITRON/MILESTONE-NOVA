#pragma once
#include "Canvas.h"
#include "LcdBus.h"
#if defined(ARDUINO) && !defined(NOVA_LCD_SYNC_TEST)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#define NOVA_LCD_ASYNC 1
#endif
namespace nova {
class Display {
public:
  ~Display();
  bool begin(uint32_t frequency, uint8_t brightness, bool inverted);
  void brightness(uint8_t value);
  void frequency(uint32_t hz);
  void inversion(bool inverted);
  void sleep();
  void present(uint32_t token = 0);
  // Sends pending strips until the time budget is spent. A video frame passes a budget larger than
  // a full-screen transfer so one frame is written in one pass instead of across several loops.
  void flush(uint32_t sliceUs = 4000);
  // Video: send these 8-row strips every frame without comparing or remembering them. Comparing a
  // 115 KB frame against the previous one costs about as much as it saves while a video plays.
  // A negative `first` ends forcing; the next frame then resends everything once.
  void forceStrips(int first, int last) {
    forcedFirst_ = first;
    forcedLast_ = last;
  }
  bool busy() const { return pending_; }
  bool canRender() const { return ready_ && (async_ ? !queued_ : !pending_); }
  void wait();
  bool ready() const { return ready_; }
  // Completed frames and total microseconds spent inside flush(), for playback diagnostics.
  uint32_t contentFrames() const { return contentFrames_; }
  uint32_t frames() const { return frames_; }
  uint64_t flushUs() const { return flushUs_; }
  Canvas &canvas() { return canvas_; }

private:
  struct Frame { uint16_t *pixels = nullptr; int first = -1, last = -1; uint32_t token = 0; };
  struct Completion { bool ok = false; uint32_t time = 0, token = 0; };
#ifdef NOVA_LCD_ASYNC
  static void task(void *self);
  QueueHandle_t work_ = nullptr, done_ = nullptr;
  TaskHandle_t worker_ = nullptr;
#endif
  bool async_ = false, queued_ = false;
  uint16_t *spare_ = nullptr, *front_ = nullptr;
  Frame next_{};
  void launch();
  Completion transfer(const Frame &frame);
  void command(uint8_t value, const uint8_t *data = nullptr, size_t count = 0);
  LcdBus bus_;
  Canvas canvas_;
  uint16_t *sent_ = nullptr;
  unsigned strip_ = 0;
  int forcedFirst_ = -1, forcedLast_ = -1;
  bool sentStale_ = false;
  uint32_t frames_ = 0, contentFrames_ = 0, lastToken_ = 0, pendingToken_ = 0;
  uint64_t flushUs_ = 0;
  bool transferFailed_ = false;
  bool ready_ = false, pending_ = false, initial_ = true;
};
} // namespace nova

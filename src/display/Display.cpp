#include "Display.h"
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
namespace nova {
Display::~Display() {
  wait();
#ifdef NOVA_LCD_ASYNC
  if (worker_) {
    Frame stop{};
    xQueueSend(work_, &stop, portMAX_DELAY);
    // Worker acknowledges termination through the completion queue.
    Completion result;
    xQueueReceive(done_, &result, portMAX_DELAY);
#ifdef NOVA_LCD_THREAD_TEST
    delete worker_; // Host adapter joins after the termination acknowledgement.
#endif
    worker_ = nullptr;
  }
  if (work_) vQueueDelete(work_);
  if (done_) vQueueDelete(done_);
#endif
  free(spare_);
  free(sent_);
}
void Display::wait() {
  while (pending_) {
    flush(60000);
#ifdef NOVA_LCD_ASYNC
    if (pending_) vTaskDelay(1);
#endif
  }
}
void Display::command(uint8_t cmd, const uint8_t *data, size_t n) {
  if (!bus_.command(cmd, data, n)) { transferFailed_ = true; ready_ = pending_ = false; }
}
bool Display::begin(uint32_t hz, uint8_t light, bool inverted) {
  pinMode(board::lcdCs, OUTPUT);
  pinMode(board::lcdDc, OUTPUT);
  pinMode(board::lcdReset, OUTPUT);
  digitalWrite(board::lcdCs, HIGH);
  ledcAttach(board::backlight, 5000, 8);
  brightness(0);
  if (!bus_.begin(hz)) return false;
  digitalWrite(board::lcdReset, LOW);
  delay(10);
  digitalWrite(board::lcdReset, HIGH);
  delay(120);
  command(0x01);
  delay(150);
  command(0x11);
  delay(120);
  const uint8_t rgb565 = 0x55, madctl = board::lcdMadctl;
  command(0x3A, &rgb565, 1);
  command(0x36, &madctl, 1);
  command(inverted ? 0x21 : 0x20);
  command(0x13);
  command(0x29);
  sent_ = static_cast<uint16_t *>(
      heap_caps_malloc(board::width * board::height * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ready_ = canvas_.begin() && sent_ && !transferFailed_;
  if (ready_) {
#ifdef NOVA_LCD_ASYNC
    spare_ = static_cast<uint16_t *>(heap_caps_malloc(board::width * board::height * 2,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    work_ = xQueueCreate(1, sizeof(Frame));
    done_ = xQueueCreate(1, sizeof(Completion));
    // Keep LCD completion on the SPI interrupt/main core; leave SD/JPEG its own core.
    if (spare_ && work_ && done_ && xTaskCreatePinnedToCore(task, "nova-lcd", 4096, this,
                          2, &worker_, ARDUINO_RUNNING_CORE) == pdPASS) {
      async_ = true;
    } else {
      free(spare_); spare_ = nullptr;
      if (work_) vQueueDelete(work_);
      if (done_) vQueueDelete(done_);
      work_ = done_ = nullptr;
    }
#endif
    canvas_.clear(color::background);
    present();
    brightness(light);
  }
  return ready_;
}
void Display::brightness(uint8_t value) { ledcWrite(board::backlight, value); }
void Display::inversion(bool inverted) {
  wait();
  if (ready_)
    command(inverted ? 0x21 : 0x20);
}
void Display::frequency(uint32_t hz) { wait(); if (!bus_.frequency(hz)) ready_ = pending_ = false; }
void Display::sleep() {
  if (async_) wait();
  else pending_ = false;
  brightness(0);
  if (ready_) {
    command(0x28);
    command(0x10);
  }
  pending_ = false;
}
void Display::present(uint32_t token) {
  if (!ready_) return;
  if (async_) {
    if (queued_) return;
    next_ = {canvas_.pixels(), forcedFirst_, forcedLast_, token};
    queued_ = true;
    if (!pending_) launch();
    return;
  }
  if (!pending_) {
    strip_ = 0;
    pendingToken_ = token;
    pending_ = true;
    if (sentStale_ && forcedFirst_ < 0) { initial_ = true; sentStale_ = false; }
  }
}
void Display::launch() {
#ifdef NOVA_LCD_ASYNC
  front_ = canvas_.exchangePixels(spare_);
  spare_ = nullptr;
  Frame frame{front_, next_.first, next_.last, next_.token};
  pending_ = true;
  queued_ = false;
  xQueueSend(work_, &frame, portMAX_DELAY);
#endif
}
Display::Completion Display::transfer(const Frame &frame) {
  const uint32_t started = micros();
  bool initial = initial_ || (sentStale_ && frame.first < 0);
  if (frame.first < 0) sentStale_ = false;
  constexpr size_t count = board::width * 8;
  bool ok = true;
  for (unsigned i = 0; i < board::height / 8 && ok; ++i) {
    const auto *pixels = frame.pixels + i * count;
    const bool forced = frame.first >= 0 && int(i) >= frame.first && int(i) <= frame.last;
    if (!forced && !initial && !memcmp(pixels, sent_ + i * count, count * 2)) continue;
    const unsigned last = forced ? std::min<unsigned>(frame.last, board::height / 8 - 1) : i;
    const unsigned row = i * 8, end = last * 8 + 7;
    const uint8_t columns[]{0,0,0,239};
    const uint8_t rows[]{uint8_t(row >> 8),uint8_t(row),uint8_t(end >> 8),uint8_t(end)};
    ok = bus_.command(0x2A, columns, 4) && bus_.command(0x2B, rows, 4) &&
         bus_.command(0x2C, nullptr, 0) && bus_.pixels(pixels, (last - i + 1) * count * 2);
    if (ok) {
      if (forced) sentStale_ = true;
      else memcpy(sent_ + i * count, pixels, count * 2);
    }
    i = last;
  }
  if (ok) initial_ = false;
  return {ok, uint32_t(micros() - started), frame.token};
}
#ifdef NOVA_LCD_ASYNC
void Display::task(void *self) {
  auto &d = *static_cast<Display *>(self);
  Frame frame;
  while (xQueueReceive(d.work_, &frame, portMAX_DELAY) == pdTRUE) {
    if (!frame.pixels) {
      Completion stopped{};
      xQueueSend(d.done_, &stopped, portMAX_DELAY);
      vTaskDelete(nullptr);
      return;
    }
    const auto result = d.transfer(frame);
    xQueueSend(d.done_, &result, portMAX_DELAY);
  }
}
#endif
void Display::flush(uint32_t sliceUs) {
#ifdef NOVA_LCD_ASYNC
  if (async_) {
    Completion result;
    if (pending_ && xQueueReceive(done_, &result, 0) == pdTRUE) {
      spare_ = front_; front_ = nullptr;
      pending_ = false;
      flushUs_ += result.time;
      if (result.ok) {
        ++frames_;
        if (result.token && result.token != lastToken_) { ++contentFrames_; lastToken_ = result.token; }
      }
      else { ready_ = false; queued_ = false; }
      if (ready_ && queued_) launch();
    }
    return;
  }
#endif
  if (!ready_ || !pending_)
    return;
  constexpr size_t pixels = board::width * 8;
  // Sending only one strip per loop spreads a full redraw over 40 framework yields and
  // unrelated work. Batch strips for a short slice, then return to input/network handling.
  // This reduces the visible rolling update; it is not panel TE/vblank synchronization.
  const uint32_t started = micros();
  while (strip_ < board::height / 8) {
    const unsigned index = strip_++;
    const unsigned row = index * 8;
    const size_t start = row * board::width;
    auto *current = canvas_.pixels() + start;
    const bool forced = int(index) >= forcedFirst_ && int(index) <= forcedLast_ && forcedFirst_ >= 0;
    if (!forced && !initial_ && !memcmp(current, sent_ + start, pixels * 2))
      continue;
    // Forced (video) strips are contiguous in the canvas: set one window and stream them in one
    // transaction instead of a window command and transaction per 8 rows.
    const unsigned last =
        forced ? std::min<unsigned>(unsigned(forcedLast_), board::height / 8 - 1) : index;
    const unsigned endRow = last * 8 + 7;
    const size_t bytes = size_t(last - index + 1) * pixels * 2;
    const uint8_t columns[]{0, 0, 0, 239};
    const uint8_t rows[]{uint8_t(row >> 8), uint8_t(row), uint8_t(endRow >> 8), uint8_t(endRow)};
    command(0x2A, columns, 4);
    command(0x2B, rows, 4);
    command(0x2C);
    if (!ready_ || !bus_.pixels(current, bytes)) {
      ready_ = pending_ = false;
      return;
    }
    if (forced) {
      sentStale_ = true;
      strip_ = last + 1;
    } else
      memcpy(sent_ + start, current, pixels * 2);
    if (uint32_t(micros() - started) >= sliceUs)
      break;
  }
  flushUs_ += uint32_t(micros() - started);
  if (strip_ >= board::height / 8) {
    pending_ = false;
    initial_ = false;
    ++frames_;
    if (pendingToken_ && pendingToken_ != lastToken_) { ++contentFrames_; lastToken_ = pendingToken_; }
  }
}
} // namespace nova

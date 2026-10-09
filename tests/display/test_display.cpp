#include "display/Display.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
// Minimal framebuffer implementation; test the real Display.cpp transport with known pixels.
namespace nova {
Canvas::~Canvas() { free(pixels_); }
bool Canvas::begin() { pixels_ = static_cast<uint16_t *>(calloc(board::width * board::height, 2)); return pixels_; }
void Canvas::clear(uint16_t color) { std::fill(pixels_, pixels_ + board::width * board::height, color); }
}
int main() {
  nova::Display display;
  assert(display.begin(40000000, 80, false));
  for (unsigned row = 0; row < 320; ++row)
    std::fill_n(display.canvas().pixels() + row * 240, 240, row);
  display.flush();
  assert(transfers.size() == 4 && display.busy()); // Multiple strips, bounded slice.
  unsigned calls = 1;
  while (display.busy()) { display.flush(); ++calls; }
  assert(calls == 10 && transfers.size() == 40);
  for (unsigned strip = 0; strip < 40; ++strip) {
    assert(startRows[strip] == strip * 8 && transfers[strip].size() == 240 * 8);
    for (unsigned p = 0; p < 240 * 8; ++p) assert(transfers[strip][p] == strip * 8 + p / 240);
  }
  transfers.clear(); startRows.clear();
  display.present(); display.flush();
  assert(!display.busy() && transfers.empty()); // Skip unchanged frame.
  display.canvas().pixels()[319 * 240] = 12345;
  display.present(); display.flush();
  assert(!display.busy() && transfers.size() == 1 && startRows[0] == 312);
  transfers.clear(); startRows.clear();
  display.canvas().clear(0xFFFF);
  testMicros = UINT32_MAX - 1500;
  display.present(); display.flush();
  assert(transfers.size() == 4 && display.busy()); // Slice survives clock wrap.
  display.sleep();
  assert(!display.busy());
  display.flush();
  assert(transfers.size() == 4);
  // At low SPI speeds a single indivisible strip may exceed the budget; do not send another.
  pixelWriteUs = 8000;
  display.present(); display.flush();
  assert(transfers.size() == 5 && display.busy());
  // A frame with a large budget (video) is written in one pass, not across several loops.
  while (display.busy()) display.flush();
  pixelWriteUs = 1000; testMicros = 0;
  transfers.clear(); startRows.clear();
  display.canvas().clear(0x1234);
  display.present(); display.flush(60000);
  assert(transfers.size() == 40 && !display.busy());
  // Forced strips are always sent and not remembered; leaving forced mode resends everything once.
  while (display.busy()) display.flush();
  display.canvas().clear(0x2222);
  display.present(); display.flush(60000);
  assert(!display.busy());
  transfers.clear(); startRows.clear();
  display.forceStrips(4, 6);
  display.present(); display.flush(60000);
  // Unchanged, still sent: the three contiguous forced strips go out as one window and one stream.
  assert(transfers.size() == 1 && startRows.size() == 1 && startRows[0] == 32);
  assert(transfers[0].size() == 3 * 240 * 8);
  transfers.clear(); startRows.clear();
  display.present(); display.flush(60000);
  assert(transfers.size() == 1 && transfers[0].size() == 3 * 240 * 8); // Forced again; nothing else changed.
  transfers.clear(); startRows.clear();
  display.forceStrips(-1, -1);
  display.present(); display.flush(60000);
  assert(transfers.size() == 40 && !display.busy()); // One full resend after leaving video.
  transfers.clear();
  display.present(); display.flush(60000);
  assert(transfers.empty()); // Back to dirty-strip behaviour.
#ifdef ARDUINO
  nativeFail = true;
  display.canvas().clear(0x7777);
  display.present(); display.flush(60000);
  assert(!display.busy() && !display.ready()); // DMA failure cannot acknowledge a stale frame.
#endif
  std::cout << "Display batching, frame integrity, dirty strips, wrap and sleep passed\n";
}

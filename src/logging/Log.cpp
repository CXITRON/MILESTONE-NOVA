#include "Log.h"
#include "../board/Board.h"
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
namespace nova {
void startLog() {
  Serial.begin(115200);
  Serial0.setTxBufferSize(2048);
  Serial0.begin(115200, SERIAL_8N1, board::uartRx, board::uartTx);
  Serial.setTxTimeoutMs(0);
}
void log(const char *tag, const char *fmt, ...) {
  char line[240];
  int n = snprintf(line, sizeof(line), "[%s] ", tag);
  if (n < 0 || n >= int(sizeof(line) - 2))
    return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(line + n, sizeof(line) - n - 2, fmt, args);
  va_end(args);
  n = strlen(line);
  line[n++] = '\n';
  if (Serial0.availableForWrite() >= n)
    Serial0.write(reinterpret_cast<const uint8_t *>(line), n);
  if (Serial && Serial.availableForWrite() >= n)
    Serial.write(reinterpret_cast<const uint8_t *>(line), n);
}
} // namespace nova

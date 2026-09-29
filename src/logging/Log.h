#pragma once
namespace nova {
void startLog();
void log(const char *tag, const char *format, ...) __attribute__((format(printf, 2, 3)));
} // namespace nova

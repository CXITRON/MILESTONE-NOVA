#include "app/App.h"
#include <Arduino.h>
// Defer the installed core's early confirmation until the application stays responsive.
extern "C" bool verifyRollbackLater() { return true; }
namespace {
nova::App app;
}
void setup() { app.begin(); }
void loop() { app.tick(); }

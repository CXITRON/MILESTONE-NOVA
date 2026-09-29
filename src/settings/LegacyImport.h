#pragma once
#include "Values.h"
namespace nova {
// Reads legacy schema 1..12 NVS only; never changes or erases legacy namespaces.
bool readLegacySettings(Settings &settings, Secrets &secrets);
} // namespace nova

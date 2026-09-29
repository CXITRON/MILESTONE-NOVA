#pragma once
#include <cstddef>
namespace nova {
struct PortalAsset {
  const char *path;
  const char *type;
  const unsigned char *bytes;
  size_t size;
};
extern const PortalAsset portalAssets[];
extern const size_t portalAssetCount;
} // namespace nova

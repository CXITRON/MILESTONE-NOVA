#pragma once
#include <cstdint>
#include <string_view>
namespace nova {
enum class ReleaseState : uint8_t { Idle, Working, Current, Available, Ready, Success, Failed };
// Stable releases use an exact major.minor.patch version and a matching v-prefixed tag.
bool compareReleaseVersions(std::string_view candidate, std::string_view current, int &order);
bool releaseAssetUrl(std::string_view url, std::string_view version);
// Only this repository's release paths and GitHub's HTTPS asset host may receive redirects.
bool releaseRedirect(const char *url);
} // namespace nova

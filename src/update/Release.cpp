#include "Release.h"
#include "../network/Endpoints.h"
#include <array>
#include <cstdint>
namespace nova {
namespace {
bool versionParts(std::string_view text, std::array<uint32_t, 3> &out) {
  for (unsigned i = 0; i < out.size(); ++i) {
    const auto dot = text.find('.');
    const auto part = text.substr(0, dot);
    if (part.empty() || part.size() > 6 || (part.size() > 1 && part[0] == '0'))
      return false;
    out[i] = 0;
    for (const char ch : part) {
      if (ch < '0' || ch > '9')
        return false;
      out[i] = out[i] * 10 + unsigned(ch - '0');
    }
    if (i == 2)
      return dot == std::string_view::npos;
    if (dot == std::string_view::npos)
      return false;
    text.remove_prefix(dot + 1);
  }
  return false;
}
bool starts(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}
bool cleanUrl(std::string_view url) {
  if (url.size() > 2048)
    return false;
  for (unsigned char ch : url)
    if (ch <= 32 || ch >= 127 || ch == '\\' || ch == '#')
      return false;
  return true;
}
} // namespace
bool compareReleaseVersions(std::string_view candidate, std::string_view current, int &order) {
  std::array<uint32_t, 3> a{}, b{};
  if (!versionParts(candidate, a) || !versionParts(current, b))
    return false;
  order = a < b ? -1 : a > b ? 1 : 0;
  return true;
}
bool releaseAssetUrl(std::string_view url, std::string_view version) {
  constexpr std::string_view base = endpoints::releases;
  int order = 0;
  if (!cleanUrl(url) || !compareReleaseVersions(version, version, order) || !starts(url, base))
    return false;
  url.remove_prefix(base.size());
  constexpr std::string_view download = "/download/v";
  if (!starts(url, download))
    return false;
  url.remove_prefix(download.size());
  if (!starts(url, version) || url.size() <= version.size() || url[version.size()] != '/')
    return false;
  url.remove_prefix(version.size() + 1);
  if (url.size() < 5 || url.size() > 96 || url.substr(url.size() - 4) != ".bin" || url[0] == '.')
    return false;
  for (char ch : url)
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-'))
      return false;
  return true;
}
bool releaseRedirect(const char *text) {
  if (!text)
    return false;
  std::string_view url = text;
  if (!cleanUrl(url))
    return false;
  constexpr std::string_view cdn = "https://release-assets.githubusercontent.com/";
  if (starts(url, cdn))
    return url.size() > cdn.size();
  constexpr std::string_view base = endpoints::releases;
  if (!starts(url, base))
    return false;
  url.remove_prefix(base.size());
  if (url == "/latest/download/stable.json")
    return true;
  constexpr std::string_view download = "/download/v";
  if (!starts(url, download))
    return false;
  url.remove_prefix(download.size());
  const auto slash = url.find('/');
  if (slash == std::string_view::npos)
    return false;
  const auto version = url.substr(0, slash);
  int order = 0;
  return compareReleaseVersions(version, version, order) &&
         (url.substr(slash + 1) == "stable.json" || releaseAssetUrl(text, version));
}
} // namespace nova

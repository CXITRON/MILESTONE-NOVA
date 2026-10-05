#include "network/Http.h"
#include "update/Release.h"
#include <cassert>
#include <iostream>
using namespace nova;
int main() {
  const char *latest = "https://github.com/CXITRON/MILESTONE-NOVA/releases/latest/download/stable.json";
  const char *tagged = "https://github.com/CXITRON/MILESTONE-NOVA/releases/download/v0.2.0/stable.json";
  const char *cdn = "https://release-assets.githubusercontent.com/github-production-release-asset/id?token=example";
  const auto reset = [](std::vector<Response> next) {
    responses = std::move(next);
    requestedUrls.clear();
    responseAt = 0;
  };
  reset({{302, {{"Location", tagged}}, ""}, {302, {{"location", cdn}}, ""}, {200, {}, "test"}});
  {
    Http http;
    assert(http.open(latest, nullptr, 8000, releaseRedirect));
    assert(requestedUrls == std::vector<std::string>({latest, tagged, cdn}));
    assert(http.length() == 4 && !http.complete());
    uint8_t body[8]{};
    assert(http.read(body, sizeof(body)) == 4 && http.complete());
  }
  reset({{302, {{"Location", tagged}}, ""}});
  { Http http; assert(!http.open(latest)); assert(requestedUrls.size() == 1); }
  for (const auto *bad : {"http://github.com/CXITRON/MILESTONE-NOVA/releases/download/v0.2.0/a.bin",
                         "https://github.com/other/repo/releases/download/v0.2.0/a.bin",
                         "https://release-assets.githubusercontent.com.evil.test/a",
                         "https://release-assets.githubusercontent.com@evil.test/a",
                         "https://evil.test/a", "/relative/location", ""}) {
    reset({{302, {{"Location", bad}}, ""}});
    Http http;
    assert(!http.open(latest, nullptr, 8000, releaseRedirect) && requestedUrls.size() == 1);
  }
  reset({{302, {{"Location", std::string(2049, 'a')}}, ""}});
  { Http http; assert(!http.open(latest, nullptr, 8000, releaseRedirect)); }
  reset({{302, {{"Location", tagged}, {"Location", cdn}}, ""}});
  { Http http; assert(!http.open(latest, nullptr, 8000, releaseRedirect)); }
  reset(std::vector<Response>(5, {302, {{"Location", latest}}, ""}));
  { Http http; assert(!http.open(latest, nullptr, 8000, releaseRedirect)); assert(requestedUrls.size() == 5); }
  reset({{404, {}, "missing"}});
  { Http http; assert(!http.open(latest, nullptr, 8000, releaseRedirect)); assert(http.status() == 404); }
  reset({});
  { Http http; assert(!http.open(latest, "title=test", 8000, releaseRedirect)); }
  std::cout << "Actual HTTPS client: TLS policy, GitHub/CDN redirects, isolation, bounds and errors passed\n";
}

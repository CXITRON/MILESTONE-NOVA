#include "update/Firmware.h"
#include "network/Http.h"
#include "core/Text.h"
#include "settings/Store.h"
#include <Preferences.h>
#include <Update.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
using Bytes = std::vector<uint8_t>;
TestSD SD;
Bytes candidate, staging;
bool writeFails = false;
unsigned aborts = 0;
std::map<std::string, Bytes> packets;
Bytes response;
size_t responseAt = 0;
namespace nova {
bool Storage::request(const char *, uint32_t) { return false; }
bool Storage::execute(FileJob &job) {
  job.ok = true;
  switch (job.op) {
  case FileOp::Info:
    job.total = candidate.size(); job.ok = !candidate.empty(); break;
  case FileOp::Read:
    job.actual = std::min<size_t>(job.length, candidate.size() - std::min<size_t>(job.offset, candidate.size()));
    if (job.actual) memcpy(job.data, candidate.data() + job.offset, job.actual);
    break;
  case FileOp::UploadBegin:
    staging.clear(); break;
  case FileOp::UploadChunk:
    job.ok = !writeFails && job.offset == staging.size();
    if (job.ok) staging.insert(staging.end(), job.data, job.data + job.length);
    else strcpy(job.error, "injected SD write failure");
    break;
  case FileOp::UploadCommit:
    candidate = staging; staging.clear(); break;
  case FileOp::UploadAbort:
    ++aborts; staging.clear(); break;
  default: assert(false);
  }
  return job.ok;
}
Http::~Http() = default;
bool Http::open(const char *url, const char *form, unsigned, RedirectPolicy policy) {
  assert(!form && policy && policy(url));
  const auto it = packets.find(url);
  status_ = it == packets.end() ? 404 : 200;
  response = it == packets.end() ? Bytes{} : it->second;
  responseAt = 0;
  length_ = response.size();
  return status_ == 200;
}
int Http::read(uint8_t *out, size_t size) {
  size = std::min(size, response.size() - responseAt);
  if (size) memcpy(out, response.data() + responseAt, size);
  responseAt += size;
  return size;
}
bool Http::complete() const { return responseAt == response.size(); }
} // namespace nova
using namespace nova;
Bytes read(const char *path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return {std::istreambuf_iterator<char>(input), {}};
}
std::string sha(const Bytes &bytes) {
  SHA256Builder hash; hash.begin(); hash.add(bytes.data(), bytes.size()); hash.calculate();
  return hash.toString();
}
const std::string latest = "https://github.com/CXITRON/MILESTONE-NOVA/releases/latest/download/stable.json";
std::string fixture(const Bytes &image, const std::string &version) {
  const std::string url = "https://github.com/CXITRON/MILESTONE-NOVA/releases/download/v" + version + "/nova.bin";
  const std::string manifest = "{\"target\":\"milestone-nova-s3\",\"version\":\"" + version +
      "\",\"url\":\"" + url + "\",\"size\":" + std::to_string(image.size()) +
      ",\"sha256\":\"" + sha(image) + "\"}";
  packets.clear();
  packets[latest] = {manifest.begin(), manifest.end()};
  packets[url] = image;
  return url;
}
void run(Firmware &f, Firmware::Work work) {
  f.serviceReady(true); // This harness is the executor; production uses Portal's worker.
  assert(f.request(work)); f.process();
}
int main(int argc, char **argv) {
  assert(argc == 4);
  const auto image = read(argv[1]), publicBytes = read(argv[2]), wrongImage = read(argv[3]);
  const std::string key(publicBytes.begin(), publicBytes.end());
  const std::string version(reinterpret_cast<const char *>(image.data() + 48));
  Storage storage;
  // Exercise the actual settings store: the added preference must not change existing A/B records.
  Settings original, loaded;
  Secrets secrets;
  strcpy(original.message, "keep existing message");
  original.messageColor = 0x1234;
  SettingsStore store;
  assert(store.save(original));
  const auto records = testNvs["nova"];
  assert(store.saveAutoUpdate(true));
  assert(testNvs["nova"]["a"] == records.at("a"));
  assert(testNvs["nova"]["b"] == records.at("b"));
  SettingsStore reload;
  assert(reload.load(loaded, secrets) && reload.autoUpdate());
  assert(!strcmp(original.message, loaded.message) && loaded.messageColor == 0x1234);
  testNvsWriteFail = true;
  assert(!reload.saveAutoUpdate(false) && reload.autoUpdate());
  testNvsWriteFail = false;
  assert(reload.reset(false) && !reload.autoUpdate());

  // v0.1.8 stored Secrets with 1,089 unused bytes of OTA fields. Loading must keep the Wi-Fi
  // profiles, store the smaller record, and drop the old copy even when the partition is full.
  {
    struct V1 {
      char ssid[33], password[65], otaPassword[65], otaPublicKey[1024], apPassword[65];
      WifiProfile networks[8];
      uint8_t networkCount;
    };
    struct Rec {
      uint32_t magic, sequence;
      V1 value;
      uint32_t checksum;
    } old{};
    old.magic = 0x4E564132;
    old.sequence = 5;
    strcpy(old.value.apPassword, "ap-password");
    strcpy(old.value.otaPublicKey, "-----BEGIN PUBLIC KEY-----");
    strcpy(old.value.networks[0].ssid, "HomeNet");
    strcpy(old.value.networks[0].password, "pass-one");
    strcpy(old.value.networks[1].ssid, "Office");
    strcpy(old.value.networks[1].password, "pass-two");
    old.value.networkCount = 2;
    old.checksum = crc32(&old, offsetof(Rec, checksum));
    testNvs.clear();
    testNvs["nova-secrets"]["a"].assign(reinterpret_cast<uint8_t *>(&old),
                                        reinterpret_cast<uint8_t *>(&old) + sizeof(old));
    testNvs["nova-secrets"]["ssid"] = {'x'};
    testNvs["nova-diag"]["a"] = std::vector<uint8_t>(1744, 7);
    testNvs["nova-diag"]["b"] = std::vector<uint8_t>(1744, 8);
    testNvsCapacity = testNvsBytes() + 100; // Too full for the new record until diagnostics go.
    Settings migratedSettings;
    Secrets migrated;
    SettingsStore migrating;
    assert(migrating.load(migratedSettings, migrated));
    testNvsCapacity = 0;
    assert(migrated.networkCount == 2 && !strcmp(migrated.networks[0].ssid, "HomeNet") &&
           !strcmp(migrated.networks[1].password, "pass-two") &&
           !strcmp(migrated.apPassword, "ap-password"));
    assert(testNvs["nova-secrets"]["a"].empty() && !testNvs["nova-secrets"]["ssid"].size());
    assert(testNvs["nova-secrets"]["b"].size() > 0 && testNvs["nova-secrets"]["b"].size() < sizeof(Rec));
    assert(testNvs["nova-diag"]["a"].empty() && testNvs["nova-diag"]["b"].empty());
    Secrets again;
    Settings againSettings;
    SettingsStore second;
    assert(second.load(againSettings, again) && again.networkCount == 2 &&
           !strcmp(again.networks[1].ssid, "Office"));
  }

  // Space recovery may discard history, never the previous settings or Wi-Fi records.
  {
    testNvs.clear();
    Settings before, after, readBack;
    Secrets credentials, readSecrets;
    strcpy(before.message, "before");
    strcpy(after.message, "after");
    strcpy(credentials.networks[0].ssid, "preserve-network");
    credentials.networkCount = 1;
    SettingsStore saving;
    assert(saving.save(before) && saving.saveSecrets(credentials));
    const auto keptSecrets = testNvs["nova-secrets"];
    testNvs["nova-diag"]["a"] = Bytes(1744, 7);
    testNvsCapacity = testNvsBytes() + 100;
    assert(saving.save(after));
    assert(testNvs["nova-diag"].empty() && testNvs["nova-secrets"] == keptSecrets);
    assert(saving.load(readBack, readSecrets) && !strcmp(readBack.message, "after"));
    testNvsCapacity = 0;
    testNvs["nova-diag"]["a"] = Bytes(1744, 7);
    const auto history = testNvs["nova-diag"];
    testNvsWriteFail = true;
    assert(!saving.save(before) && !strcmp(saving.lastError(), "ESP_FAIL"));
    assert(testNvs["nova-diag"] == history); // I/O failure is not a capacity failure.
    testNvsWriteFail = false;
    assert(saving.load(readBack, readSecrets) && !strcmp(readBack.message, "after"));
    // If even clearing history cannot help, retain both committed records.
    const auto settingsRecords = testNvs["nova"];
    const auto secretsRecords = testNvs["nova-secrets"];
    testNvsCapacity = 1;
    assert(!saving.save(before) && !strcmp(saving.lastError(), "ESP_ERR_NVS_NOT_ENOUGH_SPACE"));
    assert(testNvs["nova"] == settingsRecords && testNvs["nova-secrets"] == secretsRecords);
    testNvsCapacity = 0;
  }

  Firmware noWorker;
  noWorker.begin(storage, key.c_str());
  for (auto work : {Firmware::Work::Check, Firmware::Work::Prepare, Firmware::Work::Rollback}) {
    assert(!noWorker.request(work) && !noWorker.busy());
  }
  noWorker.serviceReady(true);
  assert(noWorker.request(Firmware::Work::Check));
  noWorker.process();
  noWorker.serviceReady(false);
  assert(!noWorker.request(Firmware::Work::Check) && !noWorker.busy());

  Firmware normal;
  normal.begin(storage, key.c_str());
  fixture(image, version);
  run(normal, Firmware::Work::Check);
  assert(normal.state() == Firmware::State::Available);
  run(normal, Firmware::Work::Download);
  assert(normal.state() == Firmware::State::Ready && candidate == image);
  run(normal, Firmware::Work::AutoInstall);
  assert(normal.state() == Firmware::State::Success && testFlash == image && testFlashBegins == 1);

  // Model bootloader rollback into the older image: same signed release must not flash again.
  Firmware rolledBack;
  rolledBack.begin(storage, key.c_str());
  fixture(image, version);
  run(rolledBack, Firmware::Work::Check); run(rolledBack, Firmware::Work::Download);
  run(rolledBack, Firmware::Work::AutoInstall);
  assert(rolledBack.state() == Firmware::State::Failed && testFlashBegins == 1);
  run(rolledBack, Firmware::Work::Prepare); run(rolledBack, Firmware::Work::Install);
  assert(rolledBack.state() == Firmware::State::Success && testFlashBegins == 2);

  Firmware same;
  same.begin(storage, key.c_str());
  fixture(image, "0.0.0"); // Test executable's running version.
  run(same, Firmware::Work::Check);
  assert(same.state() == Firmware::State::Current && !same.request(Firmware::Work::Download));

  for (const auto &bad : {std::string("{broken"), std::string("[]"),
                         std::string("{\"target\":\"other-device\"}"), std::string(2050, 'x')}) {
    Firmware f; f.begin(storage, key.c_str());
    packets[latest] = {bad.begin(), bad.end()};
    run(f, Firmware::Work::Check);
    assert(f.state() == Firmware::State::Failed);
  }
  for (unsigned fault = 0; fault < 5; ++fault) {
    Firmware f; f.begin(storage, key.c_str());
    const auto url = fixture(fault == 1 ? wrongImage : image, fault == 2 ? "999.0.0" : version);
    if (fault == 0) packets[url][1000] ^= 1; // Hash changed in transit.
    if (fault == 3) writeFails = true;
    if (fault == 4) packets[url].pop_back();
    run(f, Firmware::Work::Check); run(f, Firmware::Work::Download);
    writeFails = false;
    assert(f.state() == Firmware::State::Failed && testFlashBegins == 2 && staging.empty());
  }
  assert(aborts >= 2);
  Firmware changed;
  changed.begin(storage, key.c_str());
  fixture(image, version);
  run(changed, Firmware::Work::Check); run(changed, Firmware::Work::Download);
  candidate[1000] ^= 1;
  run(changed, Firmware::Work::Install);
  assert(changed.state() == Firmware::State::Failed && testFlashBegins == 2);

  testNvs["nova-update"].clear();
  Firmware noRecord;
  noRecord.begin(storage, key.c_str());
  fixture(image, version);
  run(noRecord, Firmware::Work::Check); run(noRecord, Firmware::Work::Download);
  testNvsWriteFail = true;
  run(noRecord, Firmware::Work::AutoInstall);
  testNvsWriteFail = false;
  assert(noRecord.state() == Firmware::State::Failed && testFlashBegins == 2);
  std::cout << "Actual Firmware worker: GitHub manifest/download, RSA, changed candidates, abort, rollback retry suppression and NVS preservation passed\n";
}

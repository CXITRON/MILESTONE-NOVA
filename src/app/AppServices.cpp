#include "../core/Text.h"
#include "../logging/Log.h"
#include "../settings/LegacyImport.h"
#include "App.h"
#include <algorithm>
#include <cstring>
#include <esp_system.h>
#include <memory>
#include <new>
#include <sys/time.h>
namespace nova {
void App::applySettings(uint32_t now) {
  display_.frequency(settings_.lcdHz);
  display_.inversion(settings_.displayInverted);
  if (!screenOff_)
    display_.brightness(settings_.lcdBrightness);
  setenv("TZ", settings_.timezone, 1);
  tzset();
  network_.configure(secrets_, settings_);
  if (timer_.state() == FocusTimer::State::Idle)
    timer_.reset(settings_.focusSeconds);
  if (unsigned(navigation_.profile()) != settings_.profile)
    selectMode(static_cast<Profile>(settings_.profile), now);
  navigation_.core(settings_.coreStart);
  screen_ = navigation_.screen();
  if (network_.ap()) {
    navigation_.page(playback_.synchronized() ? Screen::Media : Screen::Setup);
    screen_ = navigation_.screen();
  }
  lastCycle_ = now;
}
void App::portalCommands(uint32_t now) {
  auto *c = portal_.receive();
  if (!c)
    return;
  c->ok = false;
  const bool busy = shutdownStarted_ || power_.pending() || firmware_.busy();
  if (busy) {
    strcpy(c->error, "기기가 종료 또는 업데이트 작업 중입니다.");
    portal_.reply(*c);
    return;
  }
  switch (c->kind) {
  case CommandKind::Settings:
    if (validSettings(c->settings) && store_.save(c->settings)) {
      const bool sorted = settings_.mediaSort != c->settings.mediaSort;
      settings_ = c->settings;
      settingsDirty_ = false;
      applySettings(now);
      if (sorted && catalog_)
        catalog_->version = UINT32_MAX;
      c->ok = true;
    }
    break;
  case CommandKind::Mode:
    selectMode(static_cast<Profile>(c->id), now);
    c->ok = store_.save(settings_);
    if (c->ok)
      settingsDirty_ = false;
    break;
  case CommandKind::WifiScan:
    c->ok = network_.scan();
    break;
  case CommandKind::WifiTest:
    c->ok = network_.test(c->wifi);
    break;
  case CommandKind::WifiUse:
    if (c->id < secrets_.networkCount)
      c->ok = network_.test(secrets_.networks[c->id]);
    break;
  case CommandKind::WifiDelete: {
    if (c->id >= secrets_.networkCount)
      break;
    auto next = std::unique_ptr<Secrets>(new (std::nothrow) Secrets(secrets_));
    if (!next)
      break;
    for (unsigned i = c->id; i + 1 < next->networkCount; ++i)
      next->networks[i] = next->networks[i + 1];
    next->networks[--next->networkCount] = WifiProfile{};
    next->ssid[0] = next->password[0] = 0;
    if (next->networkCount) {
      strcpy(next->ssid, next->networks[0].ssid);
      strcpy(next->password, next->networks[0].password);
    }
    c->ok = store_.saveSecrets(*next);
    if (c->ok) {
      secrets_ = *next;
      network_.configure(secrets_, settings_);
    }
    break;
  }
  case CommandKind::ApClose:
    closeAp(now);
    c->ok = true;
    break;
  case CommandKind::ApPassword: {
    if (c->id > 2 || (c->id == 1 && (strlen(c->text) < 8 || strlen(c->text) > 63)))
      break;
    auto next = std::unique_ptr<Secrets>(new (std::nothrow) Secrets(secrets_));
    if (!next)
      break;
    if (c->id == 1)
      strcpy(next->apPassword, c->text);
    Settings candidate = settings_;
    candidate.apMode = c->id;
    if (store_.saveSecrets(*next) && store_.save(candidate)) {
      secrets_ = *next;
      settings_ = candidate;
      c->ok = true;
    } else
      store_.saveSecrets(secrets_);
    break;
  }
  case CommandKind::Time: {
    timeval tv{static_cast<time_t>(c->epoch), 0};
    c->ok = settimeofday(&tv, nullptr) == 0;
    if (c->ok && !rtc_.write(tv.tv_sec))
      strcpy(c->error, "시스템 시간 적용됨. RTC 저장은 실패했습니다.");
    break;
  }
  case CommandKind::Ntp:
    network_.timeSync();
    c->ok = network_.connected();
    break;
  case CommandKind::LegacyImport: {
    auto imported = std::unique_ptr<Secrets>(new (std::nothrow) Secrets(secrets_));
    Settings values = settings_;
    if (imported && readLegacySettings(values, *imported) && store_.saveSecrets(*imported)) {
      if (store_.save(values)) {
        secrets_ = *imported;
        settings_ = values;
        settingsDirty_ = false;
        applySettings(now);
        c->ok = true;
      } else
        store_.saveSecrets(secrets_);
    }
    break;
  }
  case CommandKind::Defaults: {
    Settings defaults;
    c->ok = store_.saveAutoUpdate(false) && store_.save(defaults);
    if (c->ok) {
      settings_ = defaults;
      settingsDirty_ = false;
      applySettings(now);
    }
    break;
  }
  case CommandKind::FactoryReset:
    c->ok = store_.reset(true);
    if (c->ok) {
      settingsDirty_ = false;
      rebootRequested_ = true;
    }
    break;
  case CommandKind::SensorScan:
    environment_.rescan();
    c->ok = true;
    break;
  case CommandKind::SyncStart:
    if (thermalState_ < 3 && network_.ap() && c->id && c->duration && storage_.mounted()) {
      log("SYNC", "start session=%lu duration=%lu", static_cast<unsigned long>(c->id),
          static_cast<unsigned long>(c->duration));
      playback_.startSync(c->id, c->duration, now);
      ++mediaGeneration_;
      mediaValid_ = mediaFailed_ = false;
      navigation_.page(Screen::Media);
      screen_ = Screen::Media;
      c->ok = true;
    }
    break;
  case CommandKind::SyncTick:
    c->ok = thermalState_ < 3 && network_.ap() &&
            playback_.sync(c->id, c->sequence, c->position, c->playing, now);
    break;
  case CommandKind::SyncStop:
    if (!playback_.synchronized()) {
      c->ok = true;
      break;
    }
    if (playback_.session() != c->id)
      break;
    playback_.stopSync(now);
    ++mediaGeneration_;
    mediaValid_ = mediaFailed_ = false;
    navigation_.page(Screen::Setup);
    screen_ = Screen::Setup;
    c->ok = true;
    break;
  case CommandKind::ArtRefresh:
    invalidateTrackAssets(now);
    c->ok = true;
    break;
  case CommandKind::UpdateCheck:
  case CommandKind::UpdateDownload:
  case CommandKind::UpdatePrepare:
    if (!bootConfirm_.pending() && thermalState_ < 2) {
      const auto work = c->kind == CommandKind::UpdateCheck      ? Firmware::Work::Check
                        : c->kind == CommandKind::UpdateDownload ? Firmware::Work::Download
                                                                 : Firmware::Work::Prepare;
      c->ok = firmware_.request(work);
      if (c->ok) {
        navigation_.page(Screen::Updates);
        screen_ = Screen::Updates;
      }
    }
    break;
  case CommandKind::UpdateAuto:
    c->ok = store_.saveAutoUpdate(c->id != 0);
    break;
  case CommandKind::LogsClear:
    c->ok = diagnostics_.clear();
    break;
  case CommandKind::Restart:
    if (!bootConfirm_.pending()) {
      rebootRequested_ = true;
      c->ok = true;
    }
    break;
  }
  if (!c->ok && !c->error[0])
    strcpy(c->error, "요청을 적용하지 못했습니다. 기기 상태와 입력을 확인하세요.");
  // The next HTTP edit must start from this acknowledged state, not an older 250 ms snapshot.
  publish(now, true);
  portal_.reply(*c);
}
void App::publish(uint32_t now, bool force) {
  if (!snapshot_ || (!force && now - lastPublish_ < 250))
    return;
  lastPublish_ = now;
  auto &s = *snapshot_;
  s.settings = settings_;
  network_.snapshot(s.network);
  s.track = session_.track();
  s.wifiCount = secrets_.networkCount;
  for (unsigned i = 0; i < secrets_.networkCount; ++i)
    strcpy(s.wifiSaved[i], secrets_.networks[i].ssid);
  s.mode = unsigned(navigation_.profile());
  s.mediaIndex = mediaItem_;
  s.uptime = now / 1000;
  s.heap = ESP.getFreeHeap();
  s.psram = ESP.getFreePsram();
  s.position = playback_.position(now);
  s.syncSession = playback_.session();
  s.syncStale = playback_.stale();
  s.playing = playback_.playing();
  s.sd = storage_.mounted();
  s.autoUpdate = store_.autoUpdate();
  s.temperature = environment_.temperature() + settings_.temperatureOffset;
  s.humidity = std::clamp(environment_.humidity() + settings_.humidityOffset, 0.0f, 100.0f);
  s.volts = battery_.volts();
  s.sensor = settings_.environmentEnabled && environment_.valid(now);
  cleanUtf8(s.ble, sizeof(s.ble), ble_.status());
  cleanUtf8(s.message, sizeof(s.message), notice_);
  if (screen_ == Screen::Updates || screen_ == Screen::Recovery)
    firmware_.status(s.message, sizeof(s.message));
  const int n = snprintf(
      s.diagnostics, sizeof(s.diagnostics),
      "NOVA %s\nreset=%d uptime=%lus\nheap=%u min=%u PSRAM=%u stack=%u\nloop_max_us=%lu\nSD=%d "
      "writing=%d progress=%lu catalog=%u\nWi-Fi=%s ip=%s\nBLE=%s\nRTC=%d AHT=%d "
      "sample_ms=%lu\nbattery=%.3fV low=%d critical=%d\nchip=%.1fC thermal_state=%d "
      "cpu=%uMHz\nupdate_pending=%d\nnvs=%u/%u\nprofile=%u core=%u layout=%u\nsync=%lu "
      "position=%lu stale=%d\nHistory (UTC epoch, uptime, event):\n",
      board::version, int(esp_reset_reason()), static_cast<unsigned long>(s.uptime),
      unsigned(s.heap), unsigned(ESP.getMinFreeHeap()), unsigned(s.psram),
      unsigned(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned long>(loopMax_), s.sd,
      storage_.writing(), static_cast<unsigned long>(storage_.progress()),
      catalog_ ? catalog_->count : 0, s.network.status, s.network.ip, s.ble, rtc_.present(),
      s.sensor, static_cast<unsigned long>(settings_.sampleMs), s.volts, battery_.low(),
      battery_.critical(), chipTemperature_, thermalState_, unsigned(getCpuFrequencyMhz()),
      bootConfirm_.pending(), store_.nvsUsageFree(), store_.nvsUsageTotal(), s.mode,
      navigation_.core(),
      settings_.nowLayout, static_cast<unsigned long>(s.syncSession),
      static_cast<unsigned long>(s.position), s.syncStale);
  if (n > 0 && size_t(n) < sizeof(s.diagnostics))
    diagnostics_.format(s.diagnostics + n, sizeof(s.diagnostics) - n);
  portal_.publish(s);
}
} // namespace nova

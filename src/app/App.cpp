#include "App.h"
#include "../core/Text.h"
#include "../logging/Log.h"
#include <Wire.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <memory>
#include <new>
#include <sys/time.h>
namespace nova {
void App::begin() {
  gpio_deep_sleep_hold_dis();
  for (int p : {board::red, board::green, board::backlight, board::rgb})
    gpio_hold_dis(static_cast<gpio_num_t>(p));
  startLog();
  boot_ = millis();
  ota_.beginBootCheck();
  log("BOOT", "NOVA %s reset=%d PSRAM=%u", board::version, int(esp_reset_reason()),
      unsigned(ESP.getPsramSize()));
  leds_.begin();
  rgb_.begin();
  buttons_.begin();
  const bool nvs = store_.load(settings_, secrets_);
  log("SETTINGS",
      nvs ? "NVS loaded; validated defaults available" : "NVS unavailable; using defaults");
  const bool lcd =
      display_.begin(settings_.lcdHz, settings_.lcdBrightness, settings_.displayInverted);
  log("LCD",
      lcd ? "ST7789 initialized at %lu Hz (panel response unverified)"
          : "framebuffer unavailable; headless mode",
      static_cast<unsigned long>(settings_.lcdHz));
  if (lcd) {
    while (display_.busy()) {
      display_.flush();
      yield();
    }
    View boot;
    boot.screen = Screen::Boot;
    ui_.render(display_.canvas(), boot);
    display_.present();
    while (display_.busy()) {
      display_.flush();
      yield();
    }
  }
  const Settings oldSettings = settings_;
  auto oldSecrets = std::unique_ptr<Secrets>(new (std::nothrow) Secrets(secrets_));
  storage_.begin(settings_, secrets_, !store_.hasSettings());
  if (memcmp(&oldSettings, &settings_, sizeof(settings_)))
    settingsDirty_ = true;
  if (oldSecrets && memcmp(oldSecrets.get(), &secrets_, sizeof(secrets_)) &&
      !store_.saveSecrets(secrets_))
    log("SETTINGS", "credential save failed");
  display_.brightness(settings_.lcdBrightness);
  display_.frequency(settings_.lcdHz);
  display_.inversion(settings_.displayInverted);
  setenv("TZ", settings_.timezone, 1);
  tzset();
  Wire.begin(board::sda, board::scl, board::i2cHz);
  Wire.setTimeOut(15);
  time_t utc;
  if (rtc_.read(utc)) {
    timeval value{utc, 0};
    settimeofday(&value, nullptr);
    log("RTC", "valid DS3231 UTC");
  } else
    log("RTC", "absent or invalid; awaiting NTP/console time");
  battery_.begin();
  log("BAT", "calibrated ADC, divider x2; initial filter pending");
  log("AHT", "AHT10 asynchronous probe scheduled");
  log("RGB", "5 pixels with brightness limit %u", settings_.rgbBrightness);
  log("LED", "red/green PWM initialized");
  auto *memory = heap_caps_malloc(sizeof(Lyrics), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory)
    lyrics_ = new (memory) Lyrics{};
  cover_ = static_cast<uint16_t *>(
      heap_caps_malloc(rawBytes(board::artSide), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  mediaPixels_ = static_cast<uint16_t *>(
      heap_caps_malloc(rawBytes(board::mediaSide), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  log("LYRICS", lyrics_ ? "bounded parser ready" : "PSRAM unavailable; lyrics disabled");
  network_.begin(secrets_, settings_);
  log("OTA", ota_.begin(secrets_) ? "RSA/SHA256 verifier provisioned; window closed"
                                  : "disabled until password and RSA public key provisioned");
  timer_.reset(settings_.focusSeconds);
  if (auto *p = heap_caps_malloc(sizeof(MediaCatalog), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
    catalog_ = new (p) MediaCatalog{};
  if (auto *p = heap_caps_malloc(sizeof(PortalSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
    snapshot_ = new (p) PortalSnapshot{};
  diagnostics_.begin();
  char reason[64];
  snprintf(reason, sizeof(reason), "Boot reset=%d firmware=%s", int(esp_reset_reason()),
           board::version);
  diagnostics_.record(reason);
  artwork_.begin(storage_);
  firmware_.begin(storage_, secrets_.otaPublicKey);
  serviceReady_ = portal_.begin(storage_, artwork_, firmware_);
  navigation_.core(settings_.coreStart);
  navigation_.select(static_cast<Profile>(settings_.profile));
  screen_ = navigation_.screen();
  savedAt_ = lastRtc_ = lastInput_ = lastCycle_ = millis();
  log("BTN", "active-low; held startup keys suppressed");
  log("POWER", "hold MENU for OFF; wake on OK only");
  log("UI", "ready; USB console: help");
}
void App::notice(const char *message, uint32_t now) {
  cleanUtf8(notice_, sizeof(notice_), message);
  noticed_ = now;
}
void App::changeSetting(int direction, uint32_t now) {
  switch (setting_) {
  case 0:
    settings_.lcdBrightness = std::clamp(int(settings_.lcdBrightness) + direction * 8, 8, 255);
    display_.brightness(settings_.lcdBrightness);
    break;
  case 1:
    settings_.rgbBrightness = std::clamp(int(settings_.rgbBrightness) + direction * 4, 0, 96);
    break;
  case 2:
    settings_.heartbeatBrightness =
        std::clamp(int(settings_.heartbeatBrightness) + direction * 4, 0, 128);
    break;
  case 3:
    settings_.focusSeconds = std::clamp(int(settings_.focusSeconds) + direction * 60, 60, 14400);
    if (timer_.state() == FocusTimer::State::Idle)
      timer_.reset(settings_.focusSeconds);
    break;
  case 4:
    settings_.lyricsView = !settings_.lyricsView;
    break;
  default:
    return;
  }
  settingsDirty_ = true;
  savedAt_ = now;
}
void App::selectMode(Profile profile, uint32_t now) {
  if (network_.ap())
    portal_.cleanupSync();
  if (playback_.synchronized()) {
    ++mediaGeneration_;
    mediaValid_ = mediaFailed_ = false;
    playback_.load(0, now);
  }
  playback_.stopSync(now);
  navigation_.select(profile);
  settings_.profile = unsigned(profile);
  if (network_.ap())
    navigation_.page(Screen::Setup);
  screen_ = navigation_.screen();
  settingsDirty_ = true;
  savedAt_ = lastCycle_ = now;
}
void App::selectMedia(int direction, uint32_t now) {
  if (!catalog_ || !catalog_->count || playback_.synchronized())
    return;
  const unsigned count = catalog_->count;
  for (unsigned n = 0; n < count; ++n) {
    mediaItem_ = (int(mediaItem_) + int(count) + direction) % count;
    if (catalog_->entries[mediaItem_].enabled)
      break;
  }
  ++mediaGeneration_;
  mediaValid_ = mediaFailed_ = false;
  playback_.load(0, now);
  stillSince_ = now;
}
void App::closeAp(uint32_t now) {
  portal_.cleanupSync();
  playback_.stopSync(now);
  ++mediaGeneration_;
  mediaValid_ = mediaFailed_ = false;
  storage_.cancelValidation(true);
  portal_.open(false);
  apClosing_ = true;
  navigation_.select(static_cast<Profile>(settings_.profile));
  screen_ = navigation_.screen();
  notice("AP 종료 중", now);
}
void App::input(const InputEvent &e, uint32_t now) {
  if (power_.pending() || shutdownStarted_)
    return;
  lastInput_ = lastCycle_ = now;
  if (screenOff_) {
    screenOff_ = false;
    display_.brightness(settings_.lcdBrightness);
    return;
  }
  const bool updateBusy = firmware_.busy() || !ota_.quiescent() || ota_.bootPending();
  if (e.key == Key::Menu && e.press == Press::Long) {
    if (power_.request(now, updateBusy))
      ota_.close();
    else
      notice("업데이트 / 부팅 검증 대기", now);
    return;
  }
  if (e.key == Key::Menu && e.press == Press::Short) {
    navigation_.openMenu();
    screen_ = navigation_.screen();
    settingsEditing_ = false;
    return;
  }
  if (e.key == Key::Back && e.press == Press::Short) {
    if (settingsEditing_)
      settingsEditing_ = false;
    else if (network_.ap() && screen_ != Screen::Menu)
      closeAp(now);
    else {
      navigation_.back();
      screen_ = navigation_.screen();
    }
    return;
  }
  const int direction = e.key == Key::Prev ? -1 : e.key == Key::Next ? 1 : 0;
  if (screen_ == Screen::Menu) {
    if (direction)
      navigation_.move(direction);
    else if (e.key == Key::Ok && e.press == Press::Short) {
      const auto action = navigation_.action();
      if (action <= MenuAction::Now)
        selectMode(static_cast<Profile>(action), now);
      else if (action == MenuAction::Settings)
        navigation_.page(Screen::Settings);
      else if (action == MenuAction::Setup) {
        if (updateBusy || !serviceReady_)
          notice("AP 준비 불가: 업데이트 또는 메모리 상태 확인", now);
        else {
          ble_.suspend(true);
          if (network_.openAp(secrets_, settings_)) {
            storage_.cancelValidation(false);
            portal_.open(true);
            navigation_.page(Screen::Setup);
          } else
            notice("AP 시작 실패", now);
        }
      } else if (action == MenuAction::Updates)
        navigation_.page(Screen::Updates);
      else if (action == MenuAction::Recovery)
        navigation_.page(Screen::Recovery);
      else if (action == MenuAction::Diagnostics)
        navigation_.page(Screen::Diagnostics);
      else if (action == MenuAction::Restart && !updateBusy)
        rebootRequested_ = true;
      else if (action == MenuAction::Off && power_.request(now, updateBusy))
        ota_.close();
    }
    screen_ = navigation_.screen();
    return;
  }
  if (screen_ == Screen::Settings) {
    if (direction) {
      if (settingsEditing_)
        changeSetting(direction, now);
      else
        setting_ = (int(setting_) + 8 + direction) % 8;
    } else if (e.key == Key::Ok && e.press == Press::Short) {
      if (setting_ < 5)
        settingsEditing_ = !settingsEditing_;
      else if (setting_ == 5 && !updateBusy)
        network_.begin(secrets_, settings_);
      else if (setting_ == 6)
        notice(!firmware_.busy() && ota_.open() ? "OTA 수신 창 10분"
                                                : "Wi-Fi / OTA 키 / 업데이트 상태 확인",
               now);
      else if (setting_ == 7 && power_.request(now, updateBusy))
        ota_.close();
    }
    return;
  }
  if (screen_ == Screen::Updates || screen_ == Screen::Recovery) {
    if (direction)
      recoveryItem_ = (int(recoveryItem_) + 4 + direction) % 4;
    if (e.key == Key::Ok && !updateBusy && thermalState_ < 2) {
      bool accepted = false;
      if (e.press == Press::Long)
        accepted = firmware_.request(recoveryItem_ == 3 ? Firmware::Work::Rollback
                                                        : Firmware::Work::Install);
      else if (e.press == Press::Short) {
        const Firmware::Work work[]{Firmware::Work::Check, Firmware::Work::Download,
                                    Firmware::Work::Prepare, Firmware::Work::None};
        accepted = firmware_.request(work[recoveryItem_]);
      }
      if (!accepted)
        notice("후보 검증 후 길게 OK / 복구 항목 확인", now);
    }
    return;
  }
  if (screen_ == Screen::Diagnostics) {
    if (direction)
      systemPage_ = (int(systemPage_) + 12 + direction) % 12;
    return;
  }
  if (direction) {
    if (screen_ == Screen::Now) {
      if (e.press == Press::Long) {
        if (!ble_.control(direction > 0 ? 3 : 4))
          notice("iPhone이 이 명령을 지원하지 않음", now);
      } else if (e.press == Press::Short) {
        settings_.nowLayout = (int(settings_.nowLayout) + 5 + direction) % 5;
        settingsDirty_ = true;
        savedAt_ = now;
      }
    } else if (screen_ == Screen::Media) {
      if (e.press == Press::Short)
        selectMedia(direction, now);
    } else if (screen_ == Screen::System && e.press == Press::Long)
      systemPage_ = (int(systemPage_) + 12 + direction) % 12;
    else if (e.press == Press::Short) {
      navigation_.move(direction, settings_.coreMask, settings_.coreOrder);
      screen_ = navigation_.screen();
    }
    return;
  }
  if (e.key != Key::Ok)
    return;
  if (screen_ == Screen::Timer) {
    if (e.press == Press::Long)
      timer_.reset(settings_.focusSeconds);
    else if (e.press == Press::Short)
      timer_.toggle(now);
  } else if (screen_ == Screen::Now) {
    if (e.press == Press::Long) {
      settings_.nowLayout = (settings_.nowLayout + 1) % 5;
      settingsDirty_ = true;
      savedAt_ = now;
    } else if (e.press == Press::Short && !ble_.control(2))
      notice("iPhone 재생 제어 사용 불가", now);
  } else if (screen_ == Screen::Media && e.press == Press::Short) {
    if (thermalState_ >= 3)
      notice("내부 온도 높음 · 냉각 대기", now);
    else if (playback_.synchronized())
      notice("브라우저에서 재생 / 일시 정지", now);
    else {
      mediaFailed_ = false;
      playback_.toggle(now);
    }
  } else if (screen_ == Screen::System && e.press == Press::Short)
    systemPage_ = (systemPage_ + 1) % 12;
}
void App::console(const char *cmd, uint32_t now) {
  if (power_.pending() || shutdownStarted_)
    return;
  if (!strncmp(cmd, "set ", 4)) {
    char line[512];
    cleanUtf8(line, sizeof(line), cmd + 4);
    char *value = strchr(line, '=');
    if (!value) {
      notice("set key=value", now);
      return;
    }
    *value++ = 0;
    if (!applySetting(settings_, line, value)) {
      notice("Invalid setting", now);
      return;
    }
    settingsDirty_ = true;
    savedAt_ = now;
    applySettings(now);
    notice("Setting applied", now);
  } else if (!strncmp(cmd, "wifi ", 5)) {
    if (!ota_.quiescent() || firmware_.busy() || !portal_.quiescent() || ble_.connected()) {
      notice("Disconnect BLE/OTA first", now);
      return;
    }
    const char *split = strchr(cmd + 5, '|');
    if (!split || split - (cmd + 5) > 32 || strlen(split + 1) > 63 ||
        (strlen(split + 1) > 0 && strlen(split + 1) < 8)) {
      notice("wifi SSID|password", now);
      return;
    }
    memset(secrets_.ssid, 0, sizeof(secrets_.ssid));
    memcpy(secrets_.ssid, cmd + 5, split - (cmd + 5));
    strcpy(secrets_.password, split + 1);
    secrets_.networks[0] = WifiProfile{};
    strcpy(secrets_.networks[0].ssid, secrets_.ssid);
    strcpy(secrets_.networks[0].password, secrets_.password);
    if (!secrets_.networkCount)
      secrets_.networkCount = 1;
    if (store_.saveSecrets(secrets_)) {
      network_.begin(secrets_, settings_);
      notice("Wi-Fi saved", now);
    } else
      notice("NVS save failed", now);
  } else if (!strncmp(cmd, "time ", 5)) {
    char *end;
    const auto seconds = strtoll(cmd + 5, &end, 10);
    if (*end || seconds < 946684800LL || seconds > 7258118399LL) {
      notice("Invalid UTC epoch", now);
      return;
    }
    timeval tv{static_cast<time_t>(seconds), 0};
    settimeofday(&tv, nullptr);
    rtc_.write(tv.tv_sec);
    notice("Time set", now);
  } else if (!strcmp(cmd, "sleep")) {
    if (power_.request(now, firmware_.busy() || !ota_.quiescent() || ota_.bootPending()))
      ota_.close();
  } else if (!strcmp(cmd, "ota")) {
    notice(!firmware_.busy() && thermalState_ < 2 && ota_.open() ? "OTA window open"
                                                                 : "OTA unavailable",
           now);
  } else if (!strcmp(cmd, "ota-close"))
    ota_.close();
  else if (!strcmp(cmd, "status")) {
    network_.address(address_, sizeof(address_));
    log("HW", "wifi=%s ip=%s BLE=%s SD=%d battery=%.2fV", network_.status(), address_,
        ble_.status(), storage_.mounted(), battery_.volts());
    log("MEDIA", "key=%s position=%lu", session_.track().key,
        static_cast<unsigned long>(session_.position(now)));
    log("OTA", "%s progress=%u error=%d", ota_.status(), ota_.progress(), ota_.error());
  } else if (!strcmp(cmd, "help")) {
    log("UI", "set key=value | wifi SSID|password | time UTC_epoch");
    log("UI", "status | ota | ota-close | sleep; credentials are never echoed");
  } else
    notice("Unknown command; type help", now);
}
void App::invalidateTrackAssets(uint32_t now) {
  ++assetGeneration_;
  assetsNeeded_ = true;
  coverValid_ = artBlocked_ = artPending_ = false;
  artRetry_ = now - 60000;
  if (lyrics_) {
    lyrics_->count = lyrics_->used = 0;
    lyrics_->synced = false;
  }
}
void App::assets(uint32_t now) {
  const bool boundary = playback_.duration() && playback_.position(now) >= playback_.duration();
  // Advance timeouts even while the SD worker is busy or the card is unavailable.
  playback_.tick(now, settings_.mediaLoop);
  // Local catalog refreshes must not replace the browser's active Sync clock.
  if (catalog_ && !playback_.synchronized() && storage_.catalogVersion() != catalog_->version &&
      storage_.copyCatalog(*catalog_)) {
    if (settings_.mediaSort == 1)
      std::sort(
          catalog_->entries, catalog_->entries + catalog_->count,
          [](const MediaEntry &a, const MediaEntry &b) { return strcmp(a.title, b.title) < 0; });
    if (settings_.mediaSort == 2)
      std::reverse(catalog_->entries, catalog_->entries + catalog_->count);
    if (mediaItem_ >= catalog_->count)
      mediaItem_ = 0;
    if (catalog_->count && !catalog_->entries[mediaItem_].enabled)
      selectMedia(1, now);
    ++mediaGeneration_;
    mediaValid_ = mediaFailed_ = false;
    playback_.load(0, now);
    stillSince_ = now;
  }
  if (session_.generation() != lastGeneration_ || storage_.assetRevision() != lastAssetRevision_) {
    lastGeneration_ = session_.generation();
    lastAssetRevision_ = storage_.assetRevision();
    invalidateTrackAssets(now);
    log("MEDIA", "reload track assets key=%s", session_.track().key);
  }
  if (auto *result = storage_.receive()) {
    assetPending_ = false;
    assetError_ = result->error;
    if (result->request.kind == AssetKind::Track &&
        result->request.generation == assetGeneration_) {
      if (lyrics_ && result->lyricsPresent)
        LrcParser{}.parse({result->lyrics, result->lyricsBytes}, *lyrics_);
      if (cover_ && result->artPresent) {
        memcpy(cover_, result->pixels, rawBytes(board::artSide));
        coverValid_ = true;
      }
      artBlocked_ = result->blocked;
    } else if (result->request.kind == AssetKind::Media &&
               result->request.generation == mediaGeneration_) {
      const bool first = !mediaValid_;
      if (mediaPixels_ && result->artPresent) {
        memcpy(mediaPixels_, result->pixels, rawBytes(board::mediaSide));
        mediaValid_ = true;
        if (first && !playback_.synchronized()) {
          playback_.load(result->durationMs, now);
          if (thermalState_ < 3 && settings_.mediaAutoplay && result->durationMs)
            playback_.toggle(now);
          stillSince_ = now;
        }
      } else {
        mediaValid_ = false;
        mediaFailed_ = true;
        playback_.seek(playback_.position(now), false, now);
        notice("미디어 형식 / 읽기 오류 — NEXT로 다음 파일", now);
        diagnostics_.record("Media decode failed");
      }
    }
    storage_.release();
  }
  bool artOk;
  // A stale completion is consumed without overwriting the displayed pixels.
  if (cover_ && artwork_.receive(cover_, assetGeneration_, artOk)) {
    coverValid_ = artOk;
    artPending_ = false;
  }
  if (!storage_.mounted())
    assetsNeeded_ = false;
  if (!coverValid_ && !artBlocked_ && !artPending_ && !assetsNeeded_ && settings_.artworkAuto &&
      network_.connected() && !network_.ap() && session_.track().key[0] &&
      now - artRetry_ >= 60000) {
    artPending_ = serviceReady_ && artwork_.request(session_.track(), assetGeneration_, settings_);
    artRetry_ = now;
  }
  if (thermalState_ < 3 && screen_ == Screen::Media && settings_.mediaAutoplay &&
      !playback_.synchronized() && catalog_ && mediaValid_ && storage_.mounted()) {
    unsigned enabled = 0;
    for (unsigned i = 0; i < catalog_->count; ++i)
      if (catalog_->entries[i].enabled)
        ++enabled;
    const unsigned seconds = catalog_->entries[mediaItem_].seconds
                                 ? catalog_->entries[mediaItem_].seconds
                                 : settings_.mediaSeconds;
    const bool deadline = now - stillSince_ >= seconds * 1000;
    if (enabled > 1 && deadline && (!playback_.duration() || boundary))
      selectMedia(1, now);
  }
  if (assetPending_ || power_.pending() || shutdownStarted_ || !storage_.mounted())
    return;
  if (assetsNeeded_) {
    assetPending_ = storage_.request(session_.track().key, assetGeneration_);
    if (assetPending_ || !session_.track().key[0])
      assetsNeeded_ = false;
  } else if ((screen_ == Screen::Media || playback_.synchronized()) &&
             now - mediaRequested_ >= 33 && !mediaFailed_ && thermalState_ < 3) {
    if (!mediaValid_ || playback_.playing()) {
      const char *path =
          playback_.synchronized() ? "/media/sync.njv"
          : catalog_ && mediaItem_ < catalog_->count && catalog_->entries[mediaItem_].enabled
              ? catalog_->entries[mediaItem_].path
              : nullptr;
      if (path) {
        assetPending_ = storage_.requestFile(path, playback_.position(now), mediaGeneration_);
        mediaRequested_ = now;
      }
    }
  }
}
void App::shutdown(uint32_t now) {
  const bool reboot = rebootRequested_ || ota_.state() == Ota::State::Success ||
                      firmware_.state() == Firmware::State::Success;
  if (!power_.pending() && !reboot)
    return;
  if (!shutdownStarted_) {
    shutdownStarted_ = true;
    shutdownAt_ = now;
    ota_.close();
    portal_.suspend(true);
    ble_.suspend(true);
  }
  if (!ota_.quiescent() || firmware_.busy() || !portal_.suspended() || ble_.connected())
    return;
  if (!storageStopping_) {
    if (settingsDirty_ && !store_.save(settings_)) {
      notice("Settings save failed", now);
      log("SETTINGS", "save failed during shutdown");
      if (!reboot) {
        power_.cancel();
        shutdownStarted_ = false;
        portal_.suspend(false);
        portal_.open(network_.ap());
        ble_.suspend(navigation_.profile() != Profile::Now || network_.ap());
        return;
      }
    }
    settingsDirty_ = false;
    storage_.stop();
    storageStopping_ = true;
  }
  if (!storage_.stopped())
    return;
  if (reboot) {
    if (now - shutdownAt_ >= 1200)
      ESP.restart();
    return;
  }
  if (!peripheralsOff_) {
    network_.stop();
    display_.sleep();
    rgb_.off();
    leds_.off();
    Wire.end();
    peripheralsOff_ = true;
    power_.peripheralsStopped(now);
    log("POWER", "files closed; entering deep sleep after OK release");
  }
  power_.tick(now);
}
void App::render(uint32_t now) {
  if (!display_.ready() || display_.busy() || peripheralsOff_ ||
      now - rendered_ < (screen_ == Screen::Media ? 33U : 100U))
    return;
  rendered_ = now;
  network_.address(address_, sizeof(address_));
  View v;
  v.screen = now - boot_ < 3000 ? Screen::Boot : screen_;
  v.settings = &settings_;
  v.media = &session_;
  v.lyrics = lyrics_;
  v.timer = &timer_;
  v.artwork = coverValid_ ? cover_ : nullptr;
  v.mediaPixels = mediaValid_ ? mediaPixels_ : nullptr;
  v.now = now;
  v.epoch = time(nullptr);
  v.uptime = esp_timer_get_time() / 1000000;
  v.freeHeap = ESP.getFreeHeap();
  v.minHeap = ESP.getMinFreeHeap();
  v.freePsram = ESP.getFreePsram();
  v.loopMaxUs = loopMax_;
  v.environmentValid = settings_.environmentEnabled && environment_.valid(now);
  v.temperature = environment_.temperature() + settings_.temperatureOffset;
  v.humidity = std::clamp(environment_.humidity() + settings_.humidityOffset, 0.0f, 100.0f);
  v.batteryValid = battery_.valid();
  v.volts = battery_.volts();
  v.percent = battery_.percent();
  v.warning = battery_.low();
  v.wifi = network_.connected();
  v.wifiStatus = network_.status();
  v.ble = ble_.connected();
  v.bleStatus = ble_.status();
  v.sd = storage_.mounted();
  v.rtc = rtc_.present();
  v.ip = address_;
  v.setting = setting_;
  v.settingsEditing = settingsEditing_;
  v.otaStatus = ota_.status();
  v.otaPercent = ota_.progress();
  v.mediaName = playback_.synchronized()                   ? "AP Sync · 브라우저 오디오"
                : catalog_ && mediaItem_ < catalog_->count ? catalog_->entries[mediaItem_].title
                                                           : "MENU → 설정 AP에서 업로드";
  v.mediaDuration = playback_.duration();
  v.mediaPosition = playback_.position(now);
  v.mediaPlaying = playback_.playing();
  v.mode = navigation_.profile();
  v.menu = navigation_.menu();
  v.systemPage = systemPage_;
  v.recoveryItem = recoveryItem_;
  v.canPlay = ble_.canControl(2);
  v.canNext = ble_.canControl(3);
  v.canPrev = ble_.canControl(4);
  v.sync = playback_.synchronized();
  v.syncStale = playback_.stale();
  v.apPassword = network_.apPassword();
  v.chipTemperature = chipTemperature_;
  v.thermalState = thermalState_;
  v.stack = uxTaskGetStackHighWaterMark(nullptr);
  char updateText[128];
  firmware_.status(updateText, sizeof(updateText));
  v.updateStatus = updateText;
  v.updateReady = firmware_.state() == Firmware::State::Ready;
  v.updateProgress = firmware_.progress();
  v.notice = now - noticed_ < 4000 ? notice_ : "";
  if (ota_.active() || ota_.state() == Ota::State::Success || ota_.state() == Ota::State::Failed)
    v.notice = ota_.status();
  else if (ota_.bootPending())
    v.notice = "OTA runtime check (60s)";
  v.sleeping = power_.pending();
  ui_.render(display_.canvas(), v);
  display_.present();
}
void App::tick() {
  uint32_t now = millis();
  const uint32_t start = micros();
  if (lastLoop_)
    loopMax_ = std::max(loopMax_, start - lastLoop_);
  lastLoop_ = start;
  InputEvent e;
  for (unsigned n = 0; n < 5 && buttons_.poll(e); ++n)
    input(e, now);
  portalCommands(now);
  if (const char *cmd = console_.poll())
    console(cmd, now);
  if (!shutdownStarted_) {
    network_.tick(now, ble_.connected());
    if (!bleStarted_ && navigation_.profile() == Profile::Now && !network_.ap() &&
        network_.settled(now)) {
      bleStarted_ = true;
      log("BLE", ble_.begin() ? "NimBLE initialized; advertising scheduled"
                              : "initialization failed; degraded");
    }
  }
  if (bleStarted_)
    ble_.tick(session_, millis());
  now = millis();
  const bool wirelessBlocked = shutdownStarted_ || !ota_.quiescent() || firmware_.busy() ||
                               thermalState_ >= 2 || network_.ap() ||
                               navigation_.profile() != Profile::Now;
  ble_.suspend(wirelessBlocked);
  if (apClosing_ && portal_.quiescent()) {
    network_.closeAp();
    apClosing_ = false;
    storage_.cancelValidation(false);
  }
  if (ble_.controlResult() != lastControlResult_) {
    lastControlResult_ = ble_.controlResult();
    if (lastControlResult_ < 0)
      notice("AMS 명령 전송 실패", now);
  }
  WifiProfile saved;
  if (network_.takeSaved(saved)) {
    auto candidate = std::unique_ptr<Secrets>(new (std::nothrow) Secrets(secrets_));
    if (candidate) {
      rememberWifiProfile(*candidate, saved);
      if (store_.saveSecrets(*candidate)) {
        secrets_ = *candidate;
        network_.configure(secrets_, settings_);
        network_.saveResult(true);
        notice("Wi-Fi 시험 성공 · 저장됨", now);
      } else {
        network_.saveResult(false);
        notice("Wi-Fi 연결됨 · 저장 실패", now);
      }
    } else
      network_.saveResult(false);
  }
  if (settings_.cycle && navigation_.profile() == Profile::Core &&
      screen_ == Navigation::coreScreen(navigation_.core()) &&
      now - lastCycle_ >= uint32_t(settings_.cycleSeconds) * 1000) {
    navigation_.move(1, settings_.coreMask, settings_.coreOrder);
    screen_ = navigation_.screen();
    lastCycle_ = now;
  }
  if (settings_.screenOffMinutes && !screenOff_ && !network_.ap() &&
      now - lastInput_ >= uint32_t(settings_.screenOffMinutes) * 60000) {
    screenOff_ = true;
    display_.brightness(0);
  }
  assets(now);
  timer_.tick(now);
  if (!peripheralsOff_) {
    environment_.interval(settings_.sampleMs);
    if (settings_.environmentEnabled)
      environment_.tick(now);
    if (now - lastThermal_ >= 2000) {
      lastThermal_ = now;
      chipTemperature_ = temperatureRead();
      const int previous = thermalState_;
      thermalState_ = thermalLevel(chipTemperature_, previous, settings_.thermalWarn,
                                   settings_.thermalThrottle, settings_.thermalStop);
      const unsigned frequency = thermalState_ >= 2 ? 80 : 240;
      if (getCpuFrequencyMhz() != frequency && !firmware_.busy() && ota_.quiescent())
        setCpuFrequencyMhz(frequency);
      if (previous != thermalState_) {
        char message[96];
        snprintf(message, sizeof(message), "Thermal state=%d chip=%.1fC", thermalState_,
                 chipTemperature_);
        diagnostics_.record(message);
      }
      if (thermalState_ >= 3) {
        playback_.seek(playback_.position(now), false, now);
        display_.brightness(8);
        notice("내부 온도 높음 · 냉각 대기", now);
      } else if (previous >= 3 && !screenOff_)
        display_.brightness(settings_.lcdBrightness);
    }
    if (settings_.environmentLog && settings_.environmentEnabled && environment_.valid(now) &&
        now - lastEnvironmentLog_ >= settings_.logSeconds * 1000) {
      lastEnvironmentLog_ = now;
      time_t epoch = time(nullptr);
      tm local{};
      localtime_r(&epoch, &local);
      char path[80], line[256];
      if (epoch >= 1704067200) {
        strftime(path, sizeof(path), "/logs/%Y-%m-%d.csv", &local);
        snprintf(line, sizeof(line), "%lld,%.2f,%.2f,%.2f,%.2f\n", static_cast<long long>(epoch),
                 environment_.temperature() + settings_.temperatureOffset,
                 std::clamp(environment_.humidity() + settings_.humidityOffset, 0.0f, 100.0f),
                 battery_.volts(), chipTemperature_);
        if (!portal_.logEnvironment(path, line))
          log("LOG", "environment queue full");
      }
    }
    battery_.tick(now, settings_.batteryGain, settings_.batteryOffset);
    tm local{};
    const time_t epoch = time(nullptr);
    localtime_r(&epoch, &local);
    const unsigned minute = local.tm_hour * 60 + local.tm_min;
    const bool night = settings_.nightStart < settings_.nightEnd
                           ? minute >= settings_.nightStart && minute < settings_.nightEnd
                           : minute >= settings_.nightStart || minute < settings_.nightEnd;
    const float ambient = environment_.temperature() + settings_.temperatureOffset;
    const float humidity =
        std::clamp(environment_.humidity() + settings_.humidityOffset, 0.0f, 100.0f);
    const bool sensorValid = settings_.environmentEnabled && environment_.valid(now);
    const bool sensorWarning =
        sensorValid &&
        (ambient < settings_.temperatureLow || ambient >= settings_.temperatureHigh ||
         humidity < settings_.humidityLow || humidity >= settings_.humidityHigh);
    const bool sensorCritical = sensorValid && (ambient >= settings_.temperatureCritical ||
                                                humidity >= settings_.humidityCritical);
    leds_.tick(now,
               settings_.ledsEnabled
                   ? night ? std::min(settings_.heartbeatBrightness, settings_.nightBrightness)
                           : settings_.heartbeatBrightness
                   : 0,
               sensorWarning || thermalState_ >= 1 || battery_.low() || assetError_ ||
                   !storage_.mounted() || ota_.state() == Ota::State::Failed,
               sensorCritical || thermalState_ >= 3 || battery_.critical() || !display_.ready(),
               ota_.active());
    rgb_.tick(now,
              settings_.ledsEnabled
                  ? night ? std::min(settings_.rgbBrightness, settings_.nightBrightness)
                          : settings_.rgbBrightness
                  : 0,
              session_.playing(), timer_.state() == FocusTimer::State::Running,
              timer_.state() == FocusTimer::State::Finished, battery_.low(), power_.pending());
  }
  if (settingsDirty_ && !shutdownStarted_ && ota_.quiescent() && now - savedAt_ >= 2000) {
    savedAt_ = now;
    if (store_.save(settings_))
      settingsDirty_ = false;
    else
      notice("Settings save failed", now);
  }
  if (!shutdownStarted_ && network_.connected() && now - lastRtc_ >= 3600000) {
    lastRtc_ = now;
    const time_t epoch = time(nullptr);
    if (epoch >= 1704067200)
      rtc_.write(epoch);
  }
  if (!shutdownStarted_ && !rtc_.present() && network_.connected() && now - lastRtc_ >= 30000) {
    lastRtc_ = now;
    const time_t epoch = time(nullptr);
    if (epoch >= 1704067200)
      rtc_.write(epoch);
  }
  if (now - lastLog_ >= 60000) {
    lastLog_ = now;
    log("MEM", "heap=%u min=%u psram=%u stack=%u loop_max_us=%lu", unsigned(ESP.getFreeHeap()),
        unsigned(ESP.getMinFreeHeap()), unsigned(ESP.getFreePsram()),
        unsigned(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned long>(loopMax_));
  }
  ota_.checkBoot(now);
  publish(now);
  render(now);
  display_.flush();
  shutdown(millis());
  // Yield to the framework/BLE/SD; animation and playback timing never depend on this.
  vTaskDelay(1);
}
} // namespace nova

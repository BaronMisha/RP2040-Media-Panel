#include "AppController.h"

#include <Arduino.h>

#include <algorithm>

#include "AppConfig.h"
#include "Pins.h"
#include "UiDirtyFlags.h"
#include "diagnostics/ProtocolSelfTest.h"
#include "diagnostics/ReliabilitySelfTest.h"

namespace {

const char* coverErrorCode(CoverTransferResult result) {
  switch (result) {
    case CoverTransferResult::Ok:
      return nullptr;
    case CoverTransferResult::BadValue:
      return "BAD_COVER";
    case CoverTransferResult::BadMediaId:
      return "BAD_MEDIA_ID";
    case CoverTransferResult::BadOffset:
      return "BAD_OFFSET";
    case CoverTransferResult::Incomplete:
      return "INCOMPLETE_COVER";
    case CoverTransferResult::BadCrc:
      return "BAD_CRC";
  }
  return "BAD_COVER";
}

}  // namespace

AppController::AppController(DisplayManager& displayManager,
                             UiRenderer& uiRenderer,
                             Stream& serialStream)
    : displayManager_(displayManager),
      uiRenderer_(uiRenderer),
      serialProtocol_(serialStream),
      gifScreensaver_(displayManager.display()) {}

bool AppController::begin() {
  pinMode(Pins::StatusLed, OUTPUT);
  digitalWrite(Pins::StatusLed, LOW);

  const ReliabilitySelfTest::Result selfTestResult =
      ReliabilitySelfTest::run(remoteMediaProvider_);
  if (selfTestResult != ReliabilitySelfTest::Result::Passed) {
    Serial.print(F("[ERROR] Reliability self-test failed: "));
    Serial.println(ReliabilitySelfTest::resultName(selfTestResult));
    return false;
  }
  Serial.println(F("[SELFTEST] reliability PASS"));

  const ProtocolSelfTest::Result protocolSelfTestResult =
      ProtocolSelfTest::run();
  if (protocolSelfTestResult != ProtocolSelfTest::Result::Passed) {
    Serial.print(F("[ERROR] USB protocol self-test failed: "));
    Serial.println(
        ProtocolSelfTest::resultName(protocolSelfTestResult));
    return false;
  }
  Serial.println(F("[SELFTEST] usb-protocol PASS"));

  if (!displayManager_.begin()) {
    Serial.println(F("[ERROR] Application initialization failed"));
    return false;
  }
  if (!uiRenderer_.begin()) {
    Serial.println(F("[ERROR] Primary frame buffer allocation failed"));
    return false;
  }

  const uint32_t nowMs = millis();
  remoteMediaProvider_.begin(nowMs);
  gifScreensaver_.begin(nowMs);
  lastProgressRefreshMs_ = nowMs;
  lastKaraokeRefreshMs_ = nowMs;
  lastDiscRefreshMs_ = nowMs;
  lastTimeRefreshMs_ = nowMs;
  startedMs_ = nowMs;
  lastHeartbeatToggleMs_ = nowMs;
  lastHealthLogMs_ = nowMs;
  transitionCount_ = 0;
  heartbeatLedOn_ = true;
  digitalWrite(Pins::StatusLed, HIGH);
  ready_ = true;

  serialProtocol_.begin();
  Serial.println(F("[INFO] GIF screensaver started"));
  return true;
}

void AppController::update() {
  if (!ready_) {
    return;
  }

  const uint32_t nowMs = millis();
  updateHeartbeat(nowMs);
  logHealth(nowMs);

  const bool remoteWasActive = remoteMediaProvider_.active();
  remoteMediaProvider_.update(nowMs);
  const MediaState& beforeState = remoteMediaProvider_.state();
  const bool lyricsBefore =
      beforeState.karaoke != nullptr &&
      beforeState.karaoke->available;

  ProtocolStatus protocolStatus{};
  protocolStatus.uptimeMs = nowMs - startedMs_;
  protocolStatus.remoteMode = remoteMediaProvider_.active();
  if (protocolStatus.remoteMode) {
    protocolStatus.positionMs = beforeState.positionMs;
    protocolStatus.durationMs = beforeState.durationMs;
    protocolStatus.volume = beforeState.volume;
    protocolStatus.trackIndex = beforeState.trackIndex;
    protocolStatus.trackCount = beforeState.trackCount;
    protocolStatus.playing = beforeState.playing;
  }

  RemoteMediaCommand remoteCommand;
  serialProtocol_.update(protocolStatus, remoteCommand);

  UiDirtyFlags dirtyFlags;
  bool trackChanged = false;
  if (remoteCommand.type != RemoteCommandType::None) {
    trackChanged = applyRemoteCommand(
        remoteCommand, nowMs, dirtyFlags);
    if (remoteCommand.type == RemoteCommandType::Media) {
      discPhaseQ16_ = 0;
      discPhaseRemainder_ = 0;
      lastDiscRefreshMs_ = nowMs;
      uiRenderer_.setDiscPhase(0);
    }
  }

  const bool remoteIsActive = remoteMediaProvider_.active();
  const MediaState& state = remoteMediaProvider_.state();
  const bool lyricsAfter =
      state.karaoke != nullptr && state.karaoke->available;

  if (remoteWasActive && !remoteIsActive) {
    transitionActive_ = false;
    gifScreensaver_.begin(nowMs);
    Serial.println(F("[INFO] GIF screensaver started"));
    return;
  }

  if (!remoteWasActive && remoteIsActive) {
    transitionActive_ = false;
    lyricsLayoutActive_ = lyricsAfter;
    uiRenderer_.setDiscPhase(discPhaseQ16_);
    uiRenderer_.render(state, UiDirtyFlags::fullRefresh());
    lastProgressRefreshMs_ = nowMs;
    lastKaraokeRefreshMs_ = nowMs;
    lastDiscRefreshMs_ = nowMs;
    lastTimeRefreshMs_ = nowMs;
    logCurrentTrack();
    Serial.println(F("[INFO] PC media mode started"));
    return;
  }

  if (!remoteIsActive) {
    gifScreensaver_.update(nowMs);
    return;
  }

  if (lyricsBefore != lyricsAfter) {
    startPrimaryTransition(
        nowMs, lyricsBefore, lyricsAfter);
  } else if (trackChanged) {
    dirtyFlags.status = true;
    dirtyFlags.primaryContent = true;
    dirtyFlags.metadata = true;
    dirtyFlags.time = true;
    dirtyFlags.progress = true;
    dirtyFlags.playbackStatus = true;
    dirtyFlags.volume = true;
    logCurrentTrack();
  }

  updateDiscAnimation(nowMs, dirtyFlags);
  if (transitionActive_) {
    updatePrimaryTransition(nowMs);
    return;
  }

  if (nowMs - lastProgressRefreshMs_ >=
      AppConfig::ProgressRefreshMs) {
    lastProgressRefreshMs_ = nowMs;
    dirtyFlags.progress = true;
  }
  if (nowMs - lastTimeRefreshMs_ >= AppConfig::TimeRefreshMs) {
    lastTimeRefreshMs_ = nowMs;
    dirtyFlags.time = true;
  }
  if (lyricsAfter &&
      nowMs - lastKaraokeRefreshMs_ >=
          AppConfig::KaraokeRefreshMs) {
    lastKaraokeRefreshMs_ = nowMs;
    dirtyFlags.lyrics = true;
  }

  uiRenderer_.render(state, dirtyFlags);
}

void AppController::startPrimaryTransition(
    uint32_t nowMs, bool fromLyrics, bool toLyrics) {
  const uint16_t currentLayout = transitionActive_
      ? transitionCurrentLayoutPermille_
      : (fromLyrics ? 1000U : 0U);
  const uint16_t targetLayout = toLyrics ? 1000U : 0U;
  const uint16_t distance = currentLayout > targetLayout
      ? currentLayout - targetLayout
      : targetLayout - currentLayout;

  transitionStartLayoutPermille_ = currentLayout;
  transitionCurrentLayoutPermille_ = currentLayout;
  transitionTargetLayoutPermille_ = targetLayout;
  transitionToLyrics_ = toLyrics;
  transitionStartedMs_ = nowMs;
  lastTransitionFrameMs_ =
      nowMs - AppConfig::TrackTransitionFrameMs;
  transitionDurationMs_ = std::max<uint32_t>(
      1U, static_cast<uint32_t>(
              static_cast<uint64_t>(
                  AppConfig::TrackTransitionDurationMs) *
              distance / 1000U));
  transitionActive_ = true;
  ++transitionCount_;
}

void AppController::updatePrimaryTransition(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - transitionStartedMs_;
  const bool complete = elapsed >= transitionDurationMs_;
  if (!complete &&
      nowMs - lastTransitionFrameMs_ <
          AppConfig::TrackTransitionFrameMs) {
    return;
  }
  lastTransitionFrameMs_ = nowMs;

  const uint16_t linearPermille = complete
      ? 1000U
      : static_cast<uint16_t>(
            static_cast<uint64_t>(elapsed) * 1000U /
            transitionDurationMs_);
  const uint64_t squared =
      static_cast<uint64_t>(linearPermille) * linearPermille;
  const uint16_t easedPermille = static_cast<uint16_t>(
      squared * (3000U - 2U * linearPermille) /
      1000000ULL);
  const int32_t delta =
      static_cast<int32_t>(transitionTargetLayoutPermille_) -
      transitionStartLayoutPermille_;
  transitionCurrentLayoutPermille_ =
      static_cast<uint16_t>(
          static_cast<int32_t>(
              transitionStartLayoutPermille_) +
          delta * easedPermille / 1000);
  uiRenderer_.drawPrimaryTransitionFrame(
      currentState(), transitionCurrentLayoutPermille_);

  if (complete) {
    finishPrimaryTransition(nowMs);
  }
}

void AppController::finishPrimaryTransition(uint32_t nowMs) {
  lyricsLayoutActive_ = transitionToLyrics_;
  transitionCurrentLayoutPermille_ =
      transitionTargetLayoutPermille_;

  UiDirtyFlags dirtyFlags;
  dirtyFlags.status = true;
  dirtyFlags.metadata = true;
  dirtyFlags.time = true;
  dirtyFlags.progress = true;
  dirtyFlags.playbackStatus = true;
  dirtyFlags.volume = true;
  uiRenderer_.render(currentState(), dirtyFlags);

  transitionActive_ = false;
  lastProgressRefreshMs_ = nowMs;
  lastKaraokeRefreshMs_ = nowMs;
  lastDiscRefreshMs_ = nowMs;
  lastTimeRefreshMs_ = nowMs;
}

void AppController::updateDiscAnimation(
    uint32_t nowMs, UiDirtyFlags& dirtyFlags) {
  const MediaState& state = currentState();
  if (!state.playing) {
    lastDiscRefreshMs_ = nowMs;
    return;
  }
  const uint32_t elapsed = nowMs - lastDiscRefreshMs_;
  if (elapsed < AppConfig::DiscRefreshMs) {
    return;
  }
  lastDiscRefreshMs_ = nowMs;
  const uint64_t scaledElapsed =
      static_cast<uint64_t>(elapsed) * 65536U +
      discPhaseRemainder_;
  const uint32_t advance = static_cast<uint32_t>(
      scaledElapsed / AppConfig::DiscRevolutionMs);
  discPhaseRemainder_ = static_cast<uint32_t>(
      scaledElapsed % AppConfig::DiscRevolutionMs);
  discPhaseQ16_ = static_cast<uint16_t>(
      discPhaseQ16_ + advance);
  uiRenderer_.setDiscPhase(discPhaseQ16_);
  dirtyFlags.disc = true;
}
void AppController::updateHeartbeat(uint32_t nowMs) {
  if (nowMs - lastHeartbeatToggleMs_ <
      AppConfig::HeartbeatToggleMs) {
    return;
  }

  lastHeartbeatToggleMs_ = nowMs;
  heartbeatLedOn_ = !heartbeatLedOn_;
  digitalWrite(Pins::StatusLed,
               heartbeatLedOn_ ? HIGH : LOW);
}

void AppController::logHealth(uint32_t nowMs) {
  if (nowMs - lastHealthLogMs_ <
      AppConfig::HealthLogIntervalMs) {
    return;
  }

  lastHealthLogMs_ = nowMs;

  Serial.print(F("[HEALTH] status=OK uptime_s="));
  Serial.print((nowMs - startedMs_) / 1000U);
  Serial.print(F(" mode="));
  Serial.print(
      remoteMediaProvider_.active() ? F("PC") : F("GIF"));
  Serial.print(F(" transitions="));
  Serial.print(transitionCount_);
  Serial.print(F(" gif_frame="));
  Serial.print(gifScreensaver_.frameIndex());
  if (remoteMediaProvider_.active()) {
    const MediaState& state = remoteMediaProvider_.state();
    Serial.print(F(" track="));
    Serial.print(state.trackIndex + 1U);
    Serial.print('/');
    Serial.print(state.trackCount);
    Serial.print(F(" position_ms="));
    Serial.println(state.positionMs);
  } else {
    Serial.println();
  }
}

void AppController::logCurrentTrack() const {
  const MediaState& state = currentState();
  Serial.print(F("[INFO] Track changed: "));
  Serial.print(state.trackIndex + 1U);
  Serial.print('/');
  Serial.print(state.trackCount);
  Serial.print(F(" - "));
  Serial.println(state.title);
}

bool AppController::applyRemoteCommand(
    const RemoteMediaCommand& command, uint32_t nowMs,
    UiDirtyFlags& dirtyFlags) {
  const char* errorCode = nullptr;
  bool trackChanged = false;

  switch (command.type) {
    case RemoteCommandType::Media:
      remoteMediaProvider_.setMedia(
          command.mediaId, command.title, command.artist,
          command.album, command.durationMs, nowMs);
      trackChanged = true;
      break;

    case RemoteCommandType::State:
      trackChanged = !remoteMediaProvider_.active();
      if (!remoteMediaProvider_.setPlayback(
          command.positionMs, command.durationMs, command.volume,
          command.playing, nowMs)) {
        errorCode = "NO_MEDIA";
        trackChanged = false;
      } else {
        dirtyFlags.time = true;
        dirtyFlags.progress = true;
        dirtyFlags.playbackStatus = true;
        dirtyFlags.volume = true;
      }
      break;

    case RemoteCommandType::Release:
      trackChanged = remoteMediaProvider_.release();
      break;

    case RemoteCommandType::CoverBegin:
      errorCode = coverErrorCode(
          remoteMediaProvider_.beginCover(
              command.mediaId, command.coverWidth,
              command.coverHeight, command.coverByteCount,
              command.coverCrc32, nowMs));
      break;

    case RemoteCommandType::CoverData:
      errorCode = coverErrorCode(
          remoteMediaProvider_.appendCover(
              command.mediaId, command.coverOffset,
              command.coverData, command.coverDataLength, nowMs));
      break;

    case RemoteCommandType::CoverEnd: {
      bool coverActivated = false;
      errorCode = coverErrorCode(
          remoteMediaProvider_.finishCover(
              command.mediaId, nowMs, coverActivated));
      trackChanged = coverActivated;
      break;
    }

    case RemoteCommandType::CoverStreamBegin:
      errorCode = coverErrorCode(
          remoteMediaProvider_.beginCover(
              command.mediaId, command.coverWidth,
              command.coverHeight, command.coverByteCount,
              command.coverCrc32, nowMs));
      break;

    case RemoteCommandType::CoverStreamData: {
      CoverTransferResult result =
          remoteMediaProvider_.appendCover(
              command.mediaId, command.coverOffset,
              command.coverData, command.coverDataLength, nowMs);
      bool coverActivated = false;
      if (result == CoverTransferResult::Ok &&
          command.coverFinalChunk) {
        result = remoteMediaProvider_.finishCover(
            command.mediaId, nowMs, coverActivated);
      }
      errorCode = coverErrorCode(result);
      trackChanged = coverActivated;
      break;
    }

    case RemoteCommandType::Karaoke: {
      const bool sceneChanged =
          remoteMediaProvider_.state().karaoke == nullptr;
      if (!remoteMediaProvider_.setKaraoke(
              command.mediaId, command.karaoke, nowMs)) {
        errorCode = "BAD_MEDIA_ID";
      } else if (sceneChanged) {
        trackChanged = true;
      } else {
        dirtyFlags.lyrics = true;
      }
      break;
    }

    case RemoteCommandType::KaraokeClear: {
      const bool sceneChanged =
          remoteMediaProvider_.state().karaoke != nullptr;
      if (!remoteMediaProvider_.clearKaraoke(
              command.mediaId, nowMs)) {
        errorCode = "BAD_MEDIA_ID";
      } else if (sceneChanged) {
        trackChanged = true;
      }
      break;
    }

    case RemoteCommandType::None:
      break;
  }

  serialProtocol_.completeRemoteCommand(command, errorCode);
  return trackChanged;
}

const MediaState& AppController::currentState() const {
  return remoteMediaProvider_.state();
}

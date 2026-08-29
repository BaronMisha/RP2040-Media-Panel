#pragma once

#include <cstdint>

#include "display/DisplayManager.h"
#include "display/GifScreensaver.h"
#include "display/UiRenderer.h"
#include "media/RemoteMediaProvider.h"
#include "protocol/UsbSerialProtocol.h"

class AppController {
 public:
  AppController(DisplayManager& displayManager,
                UiRenderer& uiRenderer,
                Stream& serialStream);

  bool begin();
  void update();

 private:
  void startPrimaryTransition(uint32_t nowMs,
                              bool fromLyrics,
                              bool toLyrics);
  void updatePrimaryTransition(uint32_t nowMs);
  void finishPrimaryTransition(uint32_t nowMs);
  void updateDiscAnimation(uint32_t nowMs,
                           UiDirtyFlags& dirtyFlags);
  void updateHeartbeat(uint32_t nowMs);
  void logHealth(uint32_t nowMs);
  void logCurrentTrack() const;
  bool applyRemoteCommand(
      const RemoteMediaCommand& command, uint32_t nowMs,
      UiDirtyFlags& dirtyFlags);
  const MediaState& currentState() const;

  DisplayManager& displayManager_;
  UiRenderer& uiRenderer_;
  UsbSerialProtocol serialProtocol_;
  GifScreensaver gifScreensaver_;
  RemoteMediaProvider remoteMediaProvider_;
  uint32_t lastProgressRefreshMs_ = 0;
  uint32_t lastKaraokeRefreshMs_ = 0;
  uint32_t lastDiscRefreshMs_ = 0;
  uint32_t lastTimeRefreshMs_ = 0;
  uint32_t transitionStartedMs_ = 0;
  uint32_t lastTransitionFrameMs_ = 0;
  uint32_t transitionDurationMs_ = 0;
  uint32_t startedMs_ = 0;
  uint32_t lastHeartbeatToggleMs_ = 0;
  uint32_t lastHealthLogMs_ = 0;
  uint32_t transitionCount_ = 0;
  uint32_t discPhaseRemainder_ = 0;
  uint16_t discPhaseQ16_ = 0;
  uint16_t transitionStartLayoutPermille_ = 0;
  uint16_t transitionCurrentLayoutPermille_ = 0;
  uint16_t transitionTargetLayoutPermille_ = 0;
  bool transitionToLyrics_ = false;
  bool transitionActive_ = false;
  bool lyricsLayoutActive_ = false;
  bool heartbeatLedOn_ = false;
  bool ready_ = false;
};
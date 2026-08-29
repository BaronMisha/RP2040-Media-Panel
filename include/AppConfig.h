#pragma once

#include <Arduino.h>

namespace AppConfig {

constexpr uint32_t SerialBaud = 115200;
constexpr uint8_t DisplayRotation = 0;
constexpr uint16_t ScreenWidth = 240;
constexpr uint16_t ScreenHeight = 320;
constexpr uint32_t ProgressRefreshMs = 200;
constexpr uint32_t KaraokeRefreshMs = 100;
constexpr uint32_t DiscRefreshMs = 33;
constexpr uint32_t DiscRevolutionMs = 10000;
constexpr uint32_t TimeRefreshMs = 1000;
constexpr uint32_t TrackTransitionFrameMs = 33;
constexpr uint32_t TrackTransitionDurationMs = 450;
constexpr uint32_t HeartbeatToggleMs = 500;
constexpr uint32_t HealthLogIntervalMs = 60000;
constexpr uint32_t RemoteMediaTimeoutMs = 5000;

}  // namespace AppConfig

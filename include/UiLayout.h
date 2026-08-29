#pragma once

#include <Arduino.h>

namespace UiLayout {

constexpr int16_t ScreenWidth = 240;
constexpr int16_t ScreenHeight = 320;

constexpr int16_t StatusX = 0;
constexpr int16_t StatusY = 0;
constexpr int16_t StatusW = 240;
constexpr int16_t StatusH = 20;

constexpr int16_t PrimaryX = 0;
constexpr int16_t PrimaryY = 24;
constexpr int16_t PrimaryW = 240;
constexpr int16_t PrimaryH = 160;

constexpr int16_t FullDiscX = 40;
constexpr int16_t FullDiscY = 24;
constexpr int16_t FullDiscDiameter = 160;
constexpr int16_t LyricsDiscX = 8;
constexpr int16_t LyricsDiscY = 66;
constexpr int16_t LyricsDiscDiameter = 76;

constexpr int16_t LyricsX = 88;
constexpr int16_t LyricsY = 24;
constexpr int16_t LyricsW = 144;
constexpr int16_t LyricsH = 160;

// Compatibility aliases for the 160x160 source artwork.
constexpr int16_t CoverX = FullDiscX;
constexpr int16_t CoverY = FullDiscY;
constexpr int16_t CoverW = FullDiscDiameter;
constexpr int16_t CoverH = FullDiscDiameter;

constexpr int16_t TitleX = 12;
constexpr int16_t TitleY = 188;
constexpr int16_t TitleW = 216;
constexpr int16_t TitleH = 17;

constexpr int16_t ArtistX = 12;
constexpr int16_t ArtistY = 207;
constexpr int16_t ArtistW = 216;
constexpr int16_t ArtistH = 17;

constexpr int16_t AlbumX = 12;
constexpr int16_t AlbumY = 226;
constexpr int16_t AlbumW = 216;
constexpr int16_t AlbumH = 17;

constexpr int16_t TimeY = 250;

constexpr int16_t ProgressX = 20;
constexpr int16_t ProgressY = 264;
constexpr int16_t ProgressW = 200;
constexpr int16_t ProgressH = 6;

constexpr int16_t PlaybackStatusX = 12;
constexpr int16_t PlaybackStatusY = 282;
constexpr int16_t PlaybackStatusW = 216;
constexpr int16_t PlaybackStatusH = 14;

constexpr int16_t VolumeX = 12;
constexpr int16_t VolumeY = 305;
constexpr int16_t VolumeBarX = 78;
constexpr int16_t VolumeBarY = 308;
constexpr int16_t VolumeBarW = 150;
constexpr int16_t VolumeBarH = 5;

}  // namespace UiLayout
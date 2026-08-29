#pragma once

#include <TFT_eSPI.h>

namespace UiColors {

constexpr uint16_t Background = TFT_BLACK;
constexpr uint16_t StatusBackground = 0x0841;
constexpr uint16_t PrimaryText = TFT_WHITE;
constexpr uint16_t SecondaryText = 0x9CF3;
constexpr uint16_t Accent = 0x07FF;
constexpr uint16_t ProgressBackground = 0x3186;
constexpr uint16_t ControlBackground = 0x18E3;
constexpr uint16_t CoverBorder = 0x5ACB;
constexpr uint16_t Error = TFT_RED;

}  // namespace UiColors

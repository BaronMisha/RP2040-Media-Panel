#pragma once

#include <TFT_eSPI.h>

#include <cstddef>
#include <cstdint>

struct UiTextGlyph {
  std::uint32_t codepoint;
  std::uint32_t bitmapOffset;
  std::uint8_t width;
  std::uint8_t height;
  std::int8_t xOffset;
  std::int8_t yOffset;
  std::uint8_t advance;
};

struct UiBitmapFont {
  const UiTextGlyph* glyphs;
  const std::uint8_t* bitmap;
  std::uint16_t glyphCount;
  std::uint8_t nominalSize;
  std::uint8_t lineHeight;
};

enum class UiTextAlign : std::uint8_t {
  Left,
  Center,
};

class Utf8TextRenderer {
 public:
  explicit Utf8TextRenderer(TFT_eSPI& display);

  void drawFitted(const char* text, int16_t x, int16_t y,
                  int16_t width, int16_t height,
                  std::uint16_t foreground,
                  std::uint16_t background,
                  std::uint8_t maximumSize = 14,
                  UiTextAlign align = UiTextAlign::Left);
  int16_t measureText(const char* text,
                      std::uint8_t size) const;
  std::uint8_t lineHeight(std::uint8_t size) const;
  void drawFixedClipped(
      const char* text, int16_t x, int16_t y, int16_t width,
      int16_t height, int16_t clipX, int16_t clipY,
      int16_t clipWidth, int16_t clipHeight,
      std::uint16_t foreground, std::uint8_t size,
      UiTextAlign align = UiTextAlign::Left);

 private:
  const UiBitmapFont& selectFont(const char* text, int16_t width,
                                 std::uint8_t maximumSize) const;
  const UiTextGlyph& findGlyph(const UiBitmapFont& font,
                               std::uint32_t codepoint) const;
  const UiBitmapFont& fontForSize(std::uint8_t size) const;
  int16_t measure(const UiBitmapFont& font, const char* text) const;
  int16_t measureVisible(const UiBitmapFont& font, const char* text,
                         int16_t width, bool& truncated) const;
  void drawText(const UiBitmapFont& font, const char* text,
                int16_t x, int16_t y, int16_t clipX,
                int16_t clipY, int16_t clipWidth,
                int16_t clipHeight, int16_t availableWidth,
                std::uint16_t color, bool truncated);
  void drawGlyph(const UiBitmapFont& font,
                 const UiTextGlyph& glyph, int16_t x,
                 int16_t y, int16_t clipX, int16_t clipY,
                 int16_t clipRight, int16_t clipBottom,
                 std::uint16_t color);
  static std::uint32_t nextCodepoint(const char*& text);

  TFT_eSPI& display_;
};

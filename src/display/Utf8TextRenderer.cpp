#include "display/Utf8TextRenderer.h"

#include <algorithm>

#include "display/generated/UiFontData.h"

namespace {

constexpr std::uint32_t ReplacementCodepoint = '?';
constexpr std::uint32_t EllipsisCodepoint = 0x2026;

}  // namespace

Utf8TextRenderer::Utf8TextRenderer(TFT_eSPI& display)
    : display_(display) {}

void Utf8TextRenderer::drawFitted(
    const char* text, int16_t x, int16_t y, int16_t width,
    int16_t height, std::uint16_t foreground,
    std::uint16_t background, std::uint8_t maximumSize,
    UiTextAlign align) {
  if (width <= 0 || height <= 0) {
    return;
  }

  display_.fillRect(x, y, width, height, background);
  if (text == nullptr || *text == '\0') {
    return;
  }

  const UiBitmapFont& font =
      selectFont(text, width, maximumSize);
  bool truncated = false;
  const int16_t visibleWidth =
      measureVisible(font, text, width, truncated);
  const int16_t startX =
      align == UiTextAlign::Center
          ? x + std::max<int16_t>(0, (width - visibleWidth) / 2)
          : x;
  const int16_t startY =
      y + std::max<int16_t>(0, (height - font.lineHeight) / 2);

  drawText(font, text, startX, startY, x, y, width, height,
           width, foreground, truncated);
}

int16_t Utf8TextRenderer::measureText(
    const char* text, std::uint8_t size) const {
  return measure(fontForSize(size), text);
}

std::uint8_t Utf8TextRenderer::lineHeight(
    std::uint8_t size) const {
  return fontForSize(size).lineHeight;
}

void Utf8TextRenderer::drawFixedClipped(
    const char* text, int16_t x, int16_t y, int16_t width,
    int16_t height, int16_t clipX, int16_t clipY,
    int16_t clipWidth, int16_t clipHeight,
    std::uint16_t foreground, std::uint8_t size,
    UiTextAlign align) {
  if (text == nullptr || *text == '\0' || width <= 0 ||
      height <= 0 || clipWidth <= 0 || clipHeight <= 0) {
    return;
  }

  const UiBitmapFont& font = fontForSize(size);
  bool truncated = false;
  const int16_t visibleWidth =
      measureVisible(font, text, width, truncated);
  const int16_t startX =
      align == UiTextAlign::Center
          ? x + std::max<int16_t>(0, (width - visibleWidth) / 2)
          : x;
  const int16_t startY =
      y + std::max<int16_t>(0, (height - font.lineHeight) / 2);

  drawText(font, text, startX, startY, clipX, clipY,
           clipWidth, clipHeight, width, foreground,
           truncated);
}

const UiBitmapFont& Utf8TextRenderer::selectFont(
    const char* text, int16_t width,
    std::uint8_t maximumSize) const {
  const UiBitmapFont* fallback =
      &UiFontData::Fonts[UiFontData::FontCount - 1];
  for (std::size_t index = 0;
       index < UiFontData::FontCount; ++index) {
    const UiBitmapFont& font = UiFontData::Fonts[index];
    if (font.nominalSize > maximumSize) {
      continue;
    }
    fallback = &font;
    if (measure(font, text) <= width) {
      return font;
    }
  }
  return *fallback;
}

const UiTextGlyph& Utf8TextRenderer::findGlyph(
    const UiBitmapFont& font, std::uint32_t codepoint) const {
  std::size_t left = 0;
  std::size_t right = font.glyphCount;
  while (left < right) {
    const std::size_t middle = left + (right - left) / 2;
    const std::uint32_t value = font.glyphs[middle].codepoint;
    if (value < codepoint) {
      left = middle + 1;
    } else {
      right = middle;
    }
  }
  if (left < font.glyphCount &&
      font.glyphs[left].codepoint == codepoint) {
    return font.glyphs[left];
  }
  return findGlyph(font, ReplacementCodepoint);
}

const UiBitmapFont& Utf8TextRenderer::fontForSize(
    std::uint8_t size) const {
  for (std::size_t index = 0;
       index < UiFontData::FontCount; ++index) {
    const UiBitmapFont& font = UiFontData::Fonts[index];
    if (font.nominalSize <= size) {
      return font;
    }
  }
  return UiFontData::Fonts[UiFontData::FontCount - 1];
}

int16_t Utf8TextRenderer::measure(
    const UiBitmapFont& font, const char* text) const {
  int32_t width = 0;
  while (text != nullptr && *text != '\0') {
    width += findGlyph(font, nextCodepoint(text)).advance;
    if (width >= INT16_MAX) {
      return INT16_MAX;
    }
  }
  return static_cast<int16_t>(width);
}

int16_t Utf8TextRenderer::measureVisible(
    const UiBitmapFont& font, const char* text, int16_t width,
    bool& truncated) const {
  const int16_t fullWidth = measure(font, text);
  truncated = fullWidth > width;
  if (!truncated) {
    return fullWidth;
  }

  const int16_t ellipsisWidth =
      findGlyph(font, EllipsisCodepoint).advance;
  int16_t result = 0;
  while (text != nullptr && *text != '\0') {
    const char* next = text;
    const int16_t advance =
        findGlyph(font, nextCodepoint(next)).advance;
    if (result + advance + ellipsisWidth > width) {
      break;
    }
    result += advance;
    text = next;
  }
  return std::min<int16_t>(width, result + ellipsisWidth);
}

void Utf8TextRenderer::drawText(
    const UiBitmapFont& font, const char* text, int16_t x,
    int16_t y, int16_t clipX, int16_t clipY,
    int16_t clipWidth, int16_t clipHeight,
    int16_t availableWidth, std::uint16_t color,
    bool truncated) {
  const int16_t clipRight = clipX + clipWidth;
  const int16_t clipBottom = clipY + clipHeight;
  const int16_t ellipsisWidth =
      findGlyph(font, EllipsisCodepoint).advance;
  int16_t usedWidth = 0;

  while (text != nullptr && *text != '\0') {
    const char* next = text;
    const UiTextGlyph& glyph =
        findGlyph(font, nextCodepoint(next));
    const int16_t reserved =
        truncated ? ellipsisWidth : 0;
    if (usedWidth + glyph.advance + reserved >
        availableWidth) {
      break;
    }
    drawGlyph(font, glyph, x + usedWidth, y, clipX, clipY,
              clipRight, clipBottom, color);
    usedWidth += glyph.advance;
    text = next;
  }

  if (truncated && usedWidth + ellipsisWidth <=
                       availableWidth) {
    drawGlyph(font, findGlyph(font, EllipsisCodepoint),
              x + usedWidth, y, clipX, clipY, clipRight,
              clipBottom, color);
  }
}

void Utf8TextRenderer::drawGlyph(
    const UiBitmapFont& font, const UiTextGlyph& glyph,
    int16_t x, int16_t y, int16_t clipX, int16_t clipY,
    int16_t clipRight, int16_t clipBottom,
    std::uint16_t color) {
  if (glyph.width == 0 || glyph.height == 0) {
    return;
  }

  const int16_t glyphX = x + glyph.xOffset;
  const int16_t glyphY = y + glyph.yOffset;
  const std::uint8_t stride =
      static_cast<std::uint8_t>((glyph.width + 7U) / 8U);

  for (std::uint8_t row = 0; row < glyph.height; ++row) {
    const int16_t screenY = glyphY + row;
    if (screenY < clipY || screenY >= clipBottom) {
      continue;
    }

    int16_t runStart = -1;
    for (std::uint8_t column = 0;
         column <= glyph.width; ++column) {
      bool pixelSet = false;
      if (column < glyph.width) {
        const std::size_t byteOffset =
            glyph.bitmapOffset + row * stride + column / 8U;
        pixelSet =
            (font.bitmap[byteOffset] &
             (0x80U >> (column & 7U))) != 0;
      }

      if (pixelSet && runStart < 0) {
        runStart = column;
      } else if (!pixelSet && runStart >= 0) {
        const int16_t runX = glyphX + runStart;
        const int16_t runEnd = glyphX + column;
        const int16_t visibleX = std::max(runX, clipX);
        const int16_t visibleEnd =
            std::min(runEnd, clipRight);
        if (visibleEnd > visibleX) {
          display_.drawFastHLine(
              visibleX, screenY, visibleEnd - visibleX, color);
        }
        runStart = -1;
      }
    }
  }
}

std::uint32_t Utf8TextRenderer::nextCodepoint(
    const char*& text) {
  const auto* bytes =
      reinterpret_cast<const std::uint8_t*>(text);
  const std::uint8_t first = *bytes;
  if (first < 0x80U) {
    ++text;
    return first;
  }

  std::uint32_t codepoint = 0;
  std::uint8_t length = 0;
  std::uint32_t minimum = 0;
  if ((first & 0xE0U) == 0xC0U) {
    codepoint = first & 0x1FU;
    length = 2;
    minimum = 0x80U;
  } else if ((first & 0xF0U) == 0xE0U) {
    codepoint = first & 0x0FU;
    length = 3;
    minimum = 0x800U;
  } else if ((first & 0xF8U) == 0xF0U) {
    codepoint = first & 0x07U;
    length = 4;
    minimum = 0x10000U;
  } else {
    ++text;
    return ReplacementCodepoint;
  }

  for (std::uint8_t index = 1; index < length; ++index) {
    if ((bytes[index] & 0xC0U) != 0x80U) {
      ++text;
      return ReplacementCodepoint;
    }
    codepoint = (codepoint << 6U) | (bytes[index] & 0x3FU);
  }
  text += length;

  if (codepoint < minimum || codepoint > 0x10FFFFU ||
      (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
    return ReplacementCodepoint;
  }
  return codepoint;
}

#pragma once

#include <cstddef>
#include <cstdint>

namespace KaraokeText {

constexpr std::size_t PreviousCapacity = 96;
constexpr std::size_t CurrentCapacity = 128;
constexpr std::size_t NextCapacity = 96;

}  // namespace KaraokeText

struct KaraokeWindow {
  char previousLine[KaraokeText::PreviousCapacity]{};
  char currentLine[KaraokeText::CurrentCapacity]{};
  char nextLine[KaraokeText::NextCapacity]{};

  std::uint32_t lineStartMs = 0;
  std::uint32_t lineEndMs = 0;
  std::uint16_t highlightPermille = 0;

  bool available = false;
};

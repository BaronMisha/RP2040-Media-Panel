#include "TimeFormat.h"

#include <cstdio>

namespace TimeFormat {

void formatMmSs(uint32_t timeMs, char* destination,
                size_t destinationSize) {
  if (destinationSize == 0) {
    return;
  }

  const uint32_t totalSeconds = timeMs / 1000U;
  const uint32_t minutes = totalSeconds / 60U;
  const uint32_t seconds = totalSeconds % 60U;

  std::snprintf(destination, destinationSize, "%02lu:%02lu",
                static_cast<unsigned long>(minutes),
                static_cast<unsigned long>(seconds));
}

}  // namespace TimeFormat

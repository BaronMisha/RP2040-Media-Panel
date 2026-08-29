#pragma once

#include <cstddef>
#include <cstdint>

namespace TimeFormat {

void formatMmSs(uint32_t timeMs, char* destination,
                size_t destinationSize);

}  // namespace TimeFormat

#include "assets/DemoCovers.h"

#include "generated/DigitalRainRgb565.h"
#include "generated/LastBootRgb565.h"
#include "generated/NeonSignalRgb565.h"

namespace DemoCovers {

const CoverImage NeonSignal = {
    DemoCoverData::NeonSignalPixels,
    DemoCoverData::NeonSignalPixelsWidth,
    DemoCoverData::NeonSignalPixelsHeight,
};

const CoverImage DigitalRain = {
    DemoCoverData::DigitalRainPixels,
    DemoCoverData::DigitalRainPixelsWidth,
    DemoCoverData::DigitalRainPixelsHeight,
};

const CoverImage LastBoot = {
    DemoCoverData::LastBootPixels,
    DemoCoverData::LastBootPixelsWidth,
    DemoCoverData::LastBootPixelsHeight,
};

}  // namespace DemoCovers

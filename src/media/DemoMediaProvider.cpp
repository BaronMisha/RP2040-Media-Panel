#include "DemoMediaProvider.h"

#include <cstddef>
#include <cstring>

#include "assets/DemoCovers.h"

namespace {

struct DemoTrack {
  const char* title;
  const char* artist;
  const char* album;
  const CoverImage* cover;
  uint32_t durationMs;
  uint8_t volume;
  const struct KaraokeCue* karaokeCues;
  std::size_t karaokeCueCount;
};

struct KaraokeCue {
  uint32_t startMs;
  uint32_t endMs;
  const char* text;
};

constexpr KaraokeCue NeonSignalLyrics[] = {
    {0, 4000, u8"Город засыпает в неоновом свете"},
    {4000, 8000, u8"Ритм остаётся внутри проводов"},
    {8000, 12000, u8"Мы продолжаем движение вместе"},
    {12000, 16000, u8"Музыка ведёт нас только вперёд"},
    {16000, 20000, u8"Новый сигнал начинается вновь"},
};

constexpr KaraokeCue LastBootLyrics[] = {
    {0, 4000, u8"Система просыпается снова"},
    {4000, 8000, u8"Свет пробегает по экрану"},
    {8000, 12000, u8"Каждая строка уже готова"},
    {12000, 16000, u8"Мы продолжаем путь по плану"},
    {16000, 20000, u8"Время движется без остановки"},
    {20000, 24000, u8"Музыка звучит внутри платы"},
    {24000, 28000, u8"И начинается новый запуск"},
};

constexpr DemoTrack Tracks[] = {
    {"Neon Signal", "The Forgotten Artist", "RP2040 Sessions",
     &DemoCovers::NeonSignal, 20000, 65, NeonSignalLyrics,
     sizeof(NeonSignalLyrics) / sizeof(NeonSignalLyrics[0])},
    {"Digital Rain", "Toxikk", "Embedded Dreams",
     &DemoCovers::DigitalRain, 24000, 72, nullptr, 0},
    {"Last Boot", "Pico System", "Firmware Stories",
     &DemoCovers::LastBoot, 28000, 58, LastBootLyrics,
     sizeof(LastBootLyrics) / sizeof(LastBootLyrics[0])},
};

static_assert(sizeof(Tracks) / sizeof(Tracks[0]) ==
                  DemoMediaProvider::TrackCount,
              "Demo track count mismatch");

void copyText(char* destination, size_t destinationSize,
              const char* source) {
  if (destinationSize == 0) {
    return;
  }

  std::strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

}  // namespace

void DemoMediaProvider::begin(uint32_t nowMs) {
  lastUpdateMs_ = nowMs;
  loadTrack(0);
}

MediaUpdate DemoMediaProvider::update(uint32_t nowMs) {
  const uint32_t deltaMs = nowMs - lastUpdateMs_;
  lastUpdateMs_ = nowMs;

  if (!state_.playing || deltaMs == 0) {
    return MediaUpdate::None;
  }

  uint64_t nextPositionMs =
      static_cast<uint64_t>(state_.positionMs) + deltaMs;
  bool trackChanged = false;

  while (nextPositionMs >= state_.durationMs) {
    nextPositionMs -= state_.durationMs;
    const uint8_t nextTrack =
        static_cast<uint8_t>((state_.trackIndex + 1U) % TrackCount);
    loadTrack(nextTrack);
    trackChanged = true;
  }

  state_.positionMs = static_cast<uint32_t>(nextPositionMs);
  updateKaraoke();
  return trackChanged ? MediaUpdate::TrackChanged
                      : MediaUpdate::PositionChanged;
}

const MediaState& DemoMediaProvider::state() const {
  return state_;
}

void DemoMediaProvider::loadTrack(uint8_t trackIndex) {
  if (trackIndex >= TrackCount) {
    trackIndex = 0;
  }

  const DemoTrack& track = Tracks[trackIndex];
  copyText(state_.title, sizeof(state_.title), track.title);
  copyText(state_.artist, sizeof(state_.artist), track.artist);
  copyText(state_.album, sizeof(state_.album), track.album);

  state_.cover = track.cover;
  state_.karaoke =
      track.karaokeCueCount > 0 ? &karaokeWindow_ : nullptr;
  state_.positionMs = 0;
  state_.durationMs = track.durationMs;
  state_.volume = track.volume;
  state_.trackIndex = trackIndex;
  state_.trackCount = TrackCount;
  state_.playing = true;
  state_.coverAvailable = track.cover != nullptr;
  state_.remoteSource = false;
  updateKaraoke();
}

void DemoMediaProvider::updateKaraoke() {
  const DemoTrack& track = Tracks[state_.trackIndex];
  if (track.karaokeCues == nullptr ||
      track.karaokeCueCount == 0) {
    karaokeWindow_ = KaraokeWindow{};
    state_.karaoke = nullptr;
    return;
  }

  std::size_t currentIndex = 0;
  while (currentIndex + 1U < track.karaokeCueCount &&
         state_.positionMs >=
             track.karaokeCues[currentIndex + 1U].startMs) {
    ++currentIndex;
  }

  const KaraokeCue& current = track.karaokeCues[currentIndex];
  copyText(
      karaokeWindow_.previousLine,
      sizeof(karaokeWindow_.previousLine),
      currentIndex > 0
          ? track.karaokeCues[currentIndex - 1U].text
          : "");
  copyText(karaokeWindow_.currentLine,
           sizeof(karaokeWindow_.currentLine), current.text);
  copyText(
      karaokeWindow_.nextLine,
      sizeof(karaokeWindow_.nextLine),
      currentIndex + 1U < track.karaokeCueCount
          ? track.karaokeCues[currentIndex + 1U].text
          : "");

  karaokeWindow_.lineStartMs = current.startMs;
  karaokeWindow_.lineEndMs = current.endMs;
  if (state_.positionMs <= current.startMs) {
    karaokeWindow_.highlightPermille = 0;
  } else if (state_.positionMs >= current.endMs) {
    karaokeWindow_.highlightPermille = 1000;
  } else {
    karaokeWindow_.highlightPermille =
        static_cast<uint16_t>(
            static_cast<uint64_t>(
                state_.positionMs - current.startMs) *
            1000U / (current.endMs - current.startMs));
  }
  karaokeWindow_.available = true;
  state_.karaoke = &karaokeWindow_;
}

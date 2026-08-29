#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "MediaState.h"
#include "protocol/ProtocolParser.h"

struct ProtocolStatus {
  std::uint32_t uptimeMs;
  std::uint32_t positionMs;
  std::uint32_t durationMs;
  std::uint8_t volume;
  std::uint8_t trackIndex;
  std::uint8_t trackCount;
  bool playing;
  bool remoteMode;
};

enum class RemoteCommandType : std::uint8_t {
  None,
  Media,
  State,
  Release,
  CoverBegin,
  CoverData,
  CoverEnd,
  CoverStreamBegin,
  CoverStreamData,
  Karaoke,
  KaraokeClear,
};

constexpr std::size_t RemoteCoverChunkBytes = 960;

struct RemoteMediaCommand {
  RemoteCommandType type = RemoteCommandType::None;
  std::uint8_t protocolVersion = 1;
  std::uint16_t sequence = 0;
  std::uint32_t mediaId = 0;
  std::uint32_t positionMs = 0;
  std::uint32_t durationMs = 0;
  std::uint8_t volume = 0;
  bool playing = false;
  std::uint16_t coverWidth = 0;
  std::uint16_t coverHeight = 0;
  std::uint32_t coverByteCount = 0;
  std::uint32_t coverCrc32 = 0;
  std::uint32_t coverOffset = 0;
  std::uint8_t coverData[RemoteCoverChunkBytes]{};
  std::size_t coverDataLength = 0;
  bool coverFinalChunk = false;
  char title[MediaText::TitleCapacity]{};
  char artist[MediaText::ArtistCapacity]{};
  char album[MediaText::AlbumCapacity]{};
  KaraokeWindow karaoke{};
};

class UsbSerialProtocol {
 public:
  explicit UsbSerialProtocol(Stream& stream);

  void begin();
  void update(const ProtocolStatus& status,
              RemoteMediaCommand& remoteCommand);
  void completeRemoteCommand(
      const RemoteMediaCommand& remoteCommand,
      const char* errorCode = nullptr);

 private:
  static constexpr std::uint16_t MaxBytesPerUpdate = 2048;
  static constexpr std::uint32_t RawCoverTimeoutMs = 2000;

  bool consumeByte(char value, const ProtocolStatus& status,
                   RemoteMediaCommand& remoteCommand);
  bool consumeRawByte(std::uint8_t value, std::uint32_t nowMs,
                      RemoteMediaCommand& remoteCommand);
  bool processLine(const ProtocolStatus& status,
                   RemoteMediaCommand& remoteCommand);
  bool parseRemoteCommand(
      const ProtocolParser::Command& parsedCommand,
      RemoteMediaCommand& remoteCommand);
  void beginRawCover(const RemoteMediaCommand& command,
                     std::uint32_t nowMs);
  void resetRawCover();
  void writePrefix(std::uint8_t protocolVersion);
  void sendHello(std::uint8_t protocolVersion,
                 std::uint16_t sequence);
  void sendStatus(std::uint8_t protocolVersion,
                  std::uint16_t sequence,
                  const ProtocolStatus& status);
  void sendAck(std::uint8_t protocolVersion,
               std::uint16_t sequence, const char* command);
  void sendError(std::uint8_t protocolVersion,
                 std::uint16_t sequence, const char* code);
  void resetInput();

  Stream& stream_;
  char lineBuffer_[ProtocolParser::MaxLineLength + 1]{};
  std::size_t lineLength_ = 0;
  std::uint16_t rawSequence_ = 0;
  std::uint32_t rawMediaId_ = 0;
  std::uint32_t rawExpectedBytes_ = 0;
  std::uint32_t rawReceivedBytes_ = 0;
  std::uint32_t rawLastByteMs_ = 0;
  std::size_t rawChunkLength_ = 0;
  bool discardUntilNewline_ = false;
  bool rawCoverReceiving_ = false;
};
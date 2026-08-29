#include "protocol/UsbSerialProtocol.h"

#include <cstring>
#include <limits>

namespace {

bool parseUint32(const char* text, std::uint32_t& value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }

  std::uint32_t result = 0;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    const std::uint32_t digit =
        static_cast<std::uint32_t>(*cursor - '0');
    if (result >
        (std::numeric_limits<std::uint32_t>::max() - digit) /
            10U) {
      return false;
    }
    result = result * 10U + digit;
  }

  value = result;
  return true;
}

bool parseByte(const char* text, std::uint8_t& value) {
  std::uint32_t parsed = 0;
  if (!parseUint32(text, parsed) ||
      parsed > std::numeric_limits<std::uint8_t>::max()) {
    return false;
  }
  value = static_cast<std::uint8_t>(parsed);
  return true;
}

}  // namespace

UsbSerialProtocol::UsbSerialProtocol(Stream& stream)
    : stream_(stream) {}

void UsbSerialProtocol::begin() {
  stream_.println(
      F("@RPMP1|READY|1|YD-RP2040|ILI9341|240|320"));
}

void UsbSerialProtocol::update(
    const ProtocolStatus& status,
    RemoteMediaCommand& remoteCommand) {
  remoteCommand = RemoteMediaCommand{};
  const std::uint32_t nowMs = millis();
  if (rawCoverReceiving_ &&
      nowMs - rawLastByteMs_ >= RawCoverTimeoutMs) {
    sendError(2, rawSequence_, "COVER_TIMEOUT");
    resetRawCover();
  }

  std::uint16_t processedBytes = 0;
  while (stream_.available() > 0 &&
         processedBytes < MaxBytesPerUpdate) {
    const int value = stream_.read();
    if (value < 0) {
      break;
    }
    const bool commandReady = rawCoverReceiving_
        ? consumeRawByte(static_cast<std::uint8_t>(value),
                         nowMs, remoteCommand)
        : consumeByte(static_cast<char>(value), status,
                      remoteCommand);
    if (commandReady) {
      return;
    }
    ++processedBytes;
  }
}

void UsbSerialProtocol::completeRemoteCommand(
    const RemoteMediaCommand& remoteCommand,
    const char* errorCode) {
  if (errorCode != nullptr) {
    sendError(remoteCommand.protocolVersion,
              remoteCommand.sequence, errorCode);
    if (remoteCommand.type ==
            RemoteCommandType::CoverStreamBegin ||
        remoteCommand.type ==
            RemoteCommandType::CoverStreamData) {
      resetRawCover();
    }
    return;
  }

  switch (remoteCommand.type) {
    case RemoteCommandType::Media:
      sendAck(remoteCommand.protocolVersion,
              remoteCommand.sequence, "MEDIA");
      break;
    case RemoteCommandType::State:
      sendAck(remoteCommand.protocolVersion,
              remoteCommand.sequence, "STATE");
      break;
    case RemoteCommandType::Release:
      sendAck(remoteCommand.protocolVersion,
              remoteCommand.sequence, "RELEASE");
      break;
    case RemoteCommandType::CoverBegin:
      sendAck(remoteCommand.protocolVersion,
              remoteCommand.sequence, "COVER_BEGIN");
      break;
    case RemoteCommandType::CoverData:
      sendAck(remoteCommand.protocolVersion,
              remoteCommand.sequence, "COVER_DATA");
      break;
    case RemoteCommandType::CoverEnd:
      sendAck(remoteCommand.protocolVersion,
              remoteCommand.sequence, "COVER_END");
      break;
    case RemoteCommandType::CoverStreamBegin:
      beginRawCover(remoteCommand, millis());
      sendAck(2, remoteCommand.sequence, "COVER_READY");
      break;
    case RemoteCommandType::CoverStreamData:
      if (remoteCommand.coverFinalChunk) {
        sendAck(2, remoteCommand.sequence, "COVER_END");
      }
      break;
    case RemoteCommandType::Karaoke:
      sendAck(remoteCommand.protocolVersion,
              remoteCommand.sequence, "KARAOKE");
      break;
    case RemoteCommandType::KaraokeClear:
      sendAck(remoteCommand.protocolVersion,
              remoteCommand.sequence, "KARAOKE_CLEAR");
      break;
    case RemoteCommandType::None:
      break;
  }
}

bool UsbSerialProtocol::consumeRawByte(
    std::uint8_t value, std::uint32_t nowMs,
    RemoteMediaCommand& remoteCommand) {
  rawLastByteMs_ = nowMs;
  lineBuffer_[rawChunkLength_++] = static_cast<char>(value);
  ++rawReceivedBytes_;

  const bool finalChunk =
      rawReceivedBytes_ == rawExpectedBytes_;
  if (rawChunkLength_ < RemoteCoverChunkBytes && !finalChunk) {
    return false;
  }

  remoteCommand.type = RemoteCommandType::CoverStreamData;
  remoteCommand.protocolVersion = 2;
  remoteCommand.sequence = rawSequence_;
  remoteCommand.mediaId = rawMediaId_;
  remoteCommand.coverOffset =
      rawReceivedBytes_ - rawChunkLength_;
  remoteCommand.coverDataLength = rawChunkLength_;
  remoteCommand.coverFinalChunk = finalChunk;
  std::memcpy(remoteCommand.coverData, lineBuffer_,
              rawChunkLength_);
  rawChunkLength_ = 0;
  if (finalChunk) {
    rawCoverReceiving_ = false;
  }
  return true;
}
bool UsbSerialProtocol::consumeByte(
    char value, const ProtocolStatus& status,
    RemoteMediaCommand& remoteCommand) {
  if (value == '\r') {
    return false;
  }

  if (value == '\n') {
    bool remoteCommandReady = false;
    if (discardUntilNewline_) {
      sendError(1, 0, "LINE_TOO_LONG");
    } else if (lineLength_ > 0) {
      lineBuffer_[lineLength_] = '\0';
      remoteCommandReady = processLine(status, remoteCommand);
    }
    resetInput();
    return remoteCommandReady;
  }

  if (discardUntilNewline_) {
    return false;
  }

  if (value == '\0' ||
      lineLength_ >= ProtocolParser::MaxLineLength) {
    discardUntilNewline_ = true;
    return false;
  }

  lineBuffer_[lineLength_++] = value;
  return false;
}

bool UsbSerialProtocol::processLine(
    const ProtocolStatus& status,
    RemoteMediaCommand& remoteCommand) {
  ProtocolParser::Command command;
  const ProtocolParser::ParseResult result =
      ProtocolParser::parse(lineBuffer_, command);

  if (result == ProtocolParser::ParseResult::NotProtocol) {
    return false;
  }
  if (result != ProtocolParser::ParseResult::Ok) {
    sendError(command.protocolVersion, command.sequence,
              ProtocolParser::errorCode(result));
    return false;
  }

  switch (command.type) {
    case ProtocolParser::CommandType::Hello:
      sendHello(command.protocolVersion, command.sequence);
      break;
    case ProtocolParser::CommandType::Ping:
      sendAck(command.protocolVersion, command.sequence, "PONG");
      break;
    case ProtocolParser::CommandType::Status:
      sendStatus(command.protocolVersion, command.sequence, status);
      break;
    case ProtocolParser::CommandType::Media:
    case ProtocolParser::CommandType::State:
    case ProtocolParser::CommandType::Release:
    case ProtocolParser::CommandType::CoverBegin:
    case ProtocolParser::CommandType::CoverData:
    case ProtocolParser::CommandType::CoverEnd:
    case ProtocolParser::CommandType::CoverStream:
    case ProtocolParser::CommandType::Karaoke:
    case ProtocolParser::CommandType::KaraokeClear:
      return parseRemoteCommand(command, remoteCommand);
    case ProtocolParser::CommandType::None:
      sendError(command.protocolVersion, command.sequence,
                "UNKNOWN_COMMAND");
      break;
  }
  return false;
}
bool UsbSerialProtocol::parseRemoteCommand(
    const ProtocolParser::Command& parsedCommand,
    RemoteMediaCommand& remoteCommand) {
  remoteCommand.protocolVersion = parsedCommand.protocolVersion;
  remoteCommand.sequence = parsedCommand.sequence;

  if (parsedCommand.type ==
      ProtocolParser::CommandType::Karaoke) {
    if (!parseUint32(parsedCommand.arguments[0],
                     remoteCommand.mediaId) ||
        !parseUint32(parsedCommand.arguments[1],
                     remoteCommand.karaoke.lineStartMs) ||
        !parseUint32(parsedCommand.arguments[2],
                     remoteCommand.karaoke.lineEndMs) ||
        remoteCommand.karaoke.lineEndMs <=
            remoteCommand.karaoke.lineStartMs) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_VALUE");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }

    const ProtocolParser::Base64DecodeResult previousResult =
        ProtocolParser::decodeBase64(
            parsedCommand.arguments[3],
            remoteCommand.karaoke.previousLine,
            sizeof(remoteCommand.karaoke.previousLine));
    const ProtocolParser::Base64DecodeResult currentResult =
        ProtocolParser::decodeBase64(
            parsedCommand.arguments[4],
            remoteCommand.karaoke.currentLine,
            sizeof(remoteCommand.karaoke.currentLine));
    const ProtocolParser::Base64DecodeResult nextResult =
        ProtocolParser::decodeBase64(
            parsedCommand.arguments[5],
            remoteCommand.karaoke.nextLine,
            sizeof(remoteCommand.karaoke.nextLine));

    if (previousResult ==
            ProtocolParser::Base64DecodeResult::
                DestinationTooSmall ||
        currentResult ==
            ProtocolParser::Base64DecodeResult::
                DestinationTooSmall ||
        nextResult ==
            ProtocolParser::Base64DecodeResult::
                DestinationTooSmall) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "TEXT_TOO_LONG");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }
    if (previousResult !=
            ProtocolParser::Base64DecodeResult::Ok ||
        currentResult !=
            ProtocolParser::Base64DecodeResult::Ok ||
        nextResult != ProtocolParser::Base64DecodeResult::Ok) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_BASE64");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }
    if (remoteCommand.karaoke.currentLine[0] == '\0') {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_VALUE");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }

    remoteCommand.karaoke.available = true;
    remoteCommand.type = RemoteCommandType::Karaoke;
    return true;
  }

  if (parsedCommand.type ==
      ProtocolParser::CommandType::KaraokeClear) {
    if (!parseUint32(parsedCommand.arguments[0],
                     remoteCommand.mediaId)) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_VALUE");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }
    remoteCommand.type = RemoteCommandType::KaraokeClear;
    return true;
  }

  if (parsedCommand.type ==
          ProtocolParser::CommandType::CoverBegin ||
      parsedCommand.type ==
          ProtocolParser::CommandType::CoverStream) {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!parseUint32(parsedCommand.arguments[0],
                     remoteCommand.mediaId) ||
        !parseUint32(parsedCommand.arguments[1], width) ||
        !parseUint32(parsedCommand.arguments[2], height) ||
        !parseUint32(parsedCommand.arguments[3],
                     remoteCommand.coverByteCount) ||
        !parseUint32(parsedCommand.arguments[4],
                     remoteCommand.coverCrc32) ||
        width > std::numeric_limits<std::uint16_t>::max() ||
        height > std::numeric_limits<std::uint16_t>::max()) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_VALUE");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }

    remoteCommand.type = parsedCommand.type ==
            ProtocolParser::CommandType::CoverStream
        ? RemoteCommandType::CoverStreamBegin
        : RemoteCommandType::CoverBegin;
    remoteCommand.coverWidth = static_cast<std::uint16_t>(width);
    remoteCommand.coverHeight = static_cast<std::uint16_t>(height);
    return true;
  }

  if (parsedCommand.type ==
      ProtocolParser::CommandType::CoverData) {
    if (!parseUint32(parsedCommand.arguments[0],
                     remoteCommand.mediaId) ||
        !parseUint32(parsedCommand.arguments[1],
                     remoteCommand.coverOffset)) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_VALUE");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }

    const ProtocolParser::Base64DecodeResult decodeResult =
        ProtocolParser::decodeBase64Bytes(
            parsedCommand.arguments[2], remoteCommand.coverData,
            sizeof(remoteCommand.coverData),
            remoteCommand.coverDataLength);
    if (decodeResult ==
        ProtocolParser::Base64DecodeResult::DestinationTooSmall) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "CHUNK_TOO_LARGE");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }
    if (decodeResult != ProtocolParser::Base64DecodeResult::Ok ||
        remoteCommand.coverDataLength == 0) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_BASE64");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }

    remoteCommand.type = RemoteCommandType::CoverData;
    return true;
  }

  if (parsedCommand.type ==
      ProtocolParser::CommandType::CoverEnd) {
    if (!parseUint32(parsedCommand.arguments[0],
                     remoteCommand.mediaId)) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_VALUE");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }
    remoteCommand.type = RemoteCommandType::CoverEnd;
    return true;
  }

  if (parsedCommand.type == ProtocolParser::CommandType::Release) {
    remoteCommand.type = RemoteCommandType::Release;
    return true;
  }

  if (parsedCommand.type == ProtocolParser::CommandType::State) {
    std::uint8_t playing = 0;
    if (!parseUint32(parsedCommand.arguments[0],
                     remoteCommand.positionMs) ||
        !parseUint32(parsedCommand.arguments[1],
                     remoteCommand.durationMs) ||
        !parseByte(parsedCommand.arguments[2],
                   remoteCommand.volume) ||
        !parseByte(parsedCommand.arguments[3], playing) ||
        (remoteCommand.volume > 100 &&
         remoteCommand.volume != 255) ||
        playing > 1) {
      sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_VALUE");
      remoteCommand = RemoteMediaCommand{};
      return false;
    }

    remoteCommand.type = RemoteCommandType::State;
    remoteCommand.playing = playing == 1;
    return true;
  }

  if (parsedCommand.type != ProtocolParser::CommandType::Media ||
      !parseUint32(parsedCommand.arguments[0],
                   remoteCommand.mediaId) ||
      !parseUint32(parsedCommand.arguments[1],
                   remoteCommand.durationMs)) {
    sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_VALUE");
    remoteCommand = RemoteMediaCommand{};
    return false;
  }

  const ProtocolParser::Base64DecodeResult titleResult =
      ProtocolParser::decodeBase64(
          parsedCommand.arguments[2], remoteCommand.title,
          sizeof(remoteCommand.title));
  const ProtocolParser::Base64DecodeResult artistResult =
      ProtocolParser::decodeBase64(
          parsedCommand.arguments[3], remoteCommand.artist,
          sizeof(remoteCommand.artist));
  const ProtocolParser::Base64DecodeResult albumResult =
      ProtocolParser::decodeBase64(
          parsedCommand.arguments[4], remoteCommand.album,
          sizeof(remoteCommand.album));

  if (titleResult ==
          ProtocolParser::Base64DecodeResult::DestinationTooSmall ||
      artistResult ==
          ProtocolParser::Base64DecodeResult::DestinationTooSmall ||
      albumResult ==
          ProtocolParser::Base64DecodeResult::DestinationTooSmall) {
    sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "TEXT_TOO_LONG");
    remoteCommand = RemoteMediaCommand{};
    return false;
  }
  if (titleResult != ProtocolParser::Base64DecodeResult::Ok ||
      artistResult != ProtocolParser::Base64DecodeResult::Ok ||
      albumResult != ProtocolParser::Base64DecodeResult::Ok) {
    sendError(parsedCommand.protocolVersion,
                parsedCommand.sequence, "BAD_BASE64");
    remoteCommand = RemoteMediaCommand{};
    return false;
  }

  remoteCommand.type = RemoteCommandType::Media;
  return true;
}

void UsbSerialProtocol::beginRawCover(
    const RemoteMediaCommand& command, std::uint32_t nowMs) {
  rawSequence_ = command.sequence;
  rawMediaId_ = command.mediaId;
  rawExpectedBytes_ = command.coverByteCount;
  rawReceivedBytes_ = 0;
  rawLastByteMs_ = nowMs;
  rawChunkLength_ = 0;
  rawCoverReceiving_ = true;
  resetInput();
}

void UsbSerialProtocol::resetRawCover() {
  rawSequence_ = 0;
  rawMediaId_ = 0;
  rawExpectedBytes_ = 0;
  rawReceivedBytes_ = 0;
  rawLastByteMs_ = 0;
  rawChunkLength_ = 0;
  rawCoverReceiving_ = false;
  resetInput();
}

void UsbSerialProtocol::writePrefix(
    std::uint8_t protocolVersion) {
  stream_.print(protocolVersion == 2 ? F("@RPMP2|")
                                     : F("@RPMP1|"));
}

void UsbSerialProtocol::sendHello(
    std::uint8_t protocolVersion, std::uint16_t sequence) {
  writePrefix(protocolVersion);
  stream_.print(F("HELLO|"));
  stream_.print(sequence);
  if (protocolVersion == 2) {
    stream_.println(F("|2|YD-RP2040|ILI9341|240|320|RAW_COVER"));
  } else {
    stream_.println(F("|1|YD-RP2040|ILI9341|240|320"));
  }
}

void UsbSerialProtocol::sendStatus(
    std::uint8_t protocolVersion, std::uint16_t sequence,
    const ProtocolStatus& status) {
  writePrefix(protocolVersion);
  stream_.print(F("STATUS|"));
  stream_.print(sequence);
  stream_.print(status.remoteMode ? F("|PC|") : F("|GIF|"));
  stream_.print(status.uptimeMs);
  stream_.print('|');
  stream_.print(status.trackCount == 0
                    ? 0
                    : status.trackIndex + 1U);
  stream_.print('|');
  stream_.print(status.trackCount);
  stream_.print('|');
  stream_.print(status.positionMs);
  stream_.print('|');
  stream_.print(status.durationMs);
  stream_.print('|');
  stream_.print(status.volume);
  stream_.print('|');
  stream_.println(status.playing ? 1 : 0);
}

void UsbSerialProtocol::sendAck(
    std::uint8_t protocolVersion, std::uint16_t sequence,
    const char* command) {
  writePrefix(protocolVersion);
  stream_.print(F("ACK|"));
  stream_.print(sequence);
  stream_.print('|');
  stream_.println(command);
}

void UsbSerialProtocol::sendError(
    std::uint8_t protocolVersion, std::uint16_t sequence,
    const char* code) {
  writePrefix(protocolVersion);
  stream_.print(F("ERR|"));
  stream_.print(sequence);
  stream_.print('|');
  stream_.println(code);
}

void UsbSerialProtocol::resetInput() {
  lineLength_ = 0;
  discardUntilNewline_ = false;
}
#include "protocol/ProtocolParser.h"

#include <cstring>

namespace ProtocolParser {
namespace {

constexpr char ProtocolPrefixV1[] = "@RPMP1";
constexpr char ProtocolPrefixV2[] = "@RPMP2";
constexpr char ProtocolFamilyPrefix[] = "@RPMP";
constexpr std::size_t HeaderFieldCount = 3;
constexpr std::size_t FieldCapacity =
    HeaderFieldCount + MaxArgumentCount;

bool parseSequence(const char* text, std::uint16_t& sequence) {
  if (text == nullptr || *text == '\0') {
    return false;
  }

  std::uint32_t value = 0;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    value = value * 10U +
            static_cast<std::uint32_t>(*cursor - '0');
    if (value > MaxSequence) {
      return false;
    }
  }

  sequence = static_cast<std::uint16_t>(value);
  return true;
}

int decodeBase64Value(char value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A';
  }
  if (value >= 'a' && value <= 'z') {
    return value - 'a' + 26;
  }
  if (value >= '0' && value <= '9') {
    return value - '0' + 52;
  }
  if (value == '+') {
    return 62;
  }
  if (value == '/') {
    return 63;
  }
  return -1;
}

}  // namespace

ParseResult parse(char* line, Command& command) {
  command = Command{};
  if (line == nullptr || *line == '\0') {
    return ParseResult::NotProtocol;
  }

  char* fields[FieldCapacity] = {line, nullptr, nullptr, nullptr};
  std::size_t fieldCount = 1;

  for (char* cursor = line; *cursor != '\0'; ++cursor) {
    if (*cursor != '|') {
      continue;
    }
    if (fieldCount >= FieldCapacity) {
      return ParseResult::BadFieldCount;
    }
    *cursor = '\0';
    fields[fieldCount++] = cursor + 1;
  }

  if (std::strcmp(fields[0], ProtocolPrefixV1) == 0) {
    command.protocolVersion = 1;
  } else if (std::strcmp(fields[0], ProtocolPrefixV2) == 0) {
    command.protocolVersion = 2;
  } else {
    if (std::strncmp(fields[0], ProtocolFamilyPrefix,
                     sizeof(ProtocolFamilyPrefix) - 1U) == 0) {
      return ParseResult::UnsupportedVersion;
    }
    return ParseResult::NotProtocol;
  }

  if (fieldCount < HeaderFieldCount) {
    return ParseResult::BadFieldCount;
  }

  if (!parseSequence(fields[2], command.sequence)) {
    return ParseResult::BadSequence;
  }

  if (std::strcmp(fields[1], "HELLO") == 0) {
    command.type = CommandType::Hello;
  } else if (std::strcmp(fields[1], "PING") == 0) {
    command.type = CommandType::Ping;
  } else if (std::strcmp(fields[1], "STATUS") == 0) {
    command.type = CommandType::Status;
  } else if (std::strcmp(fields[1], "MEDIA") == 0) {
    command.type = CommandType::Media;
  } else if (std::strcmp(fields[1], "STATE") == 0) {
    command.type = CommandType::State;
  } else if (std::strcmp(fields[1], "RELEASE") == 0) {
    command.type = CommandType::Release;
  } else if (std::strcmp(fields[1], "COVER_BEGIN") == 0) {
    command.type = CommandType::CoverBegin;
  } else if (std::strcmp(fields[1], "COVER_DATA") == 0) {
    command.type = CommandType::CoverData;
  } else if (std::strcmp(fields[1], "COVER_END") == 0) {
    command.type = CommandType::CoverEnd;
  } else if (std::strcmp(fields[1], "COVER_STREAM") == 0 &&
             command.protocolVersion == 2) {
    command.type = CommandType::CoverStream;
  } else if (std::strcmp(fields[1], "KARAOKE") == 0) {
    command.type = CommandType::Karaoke;
  } else if (std::strcmp(fields[1], "KARAOKE_CLEAR") == 0) {
    command.type = CommandType::KaraokeClear;
  } else {
    return ParseResult::UnknownCommand;
  }

  std::size_t expectedFieldCount = HeaderFieldCount;
  switch (command.type) {
    case CommandType::Media:
      expectedFieldCount = 8;
      break;
    case CommandType::State:
      expectedFieldCount = 7;
      break;
    case CommandType::CoverBegin:
    case CommandType::CoverStream:
      expectedFieldCount = 8;
      break;
    case CommandType::CoverData:
      expectedFieldCount = 6;
      break;
    case CommandType::CoverEnd:
      expectedFieldCount = 4;
      break;
    case CommandType::Karaoke:
      expectedFieldCount = 9;
      break;
    case CommandType::KaraokeClear:
      expectedFieldCount = 4;
      break;
    case CommandType::None:
    case CommandType::Hello:
    case CommandType::Ping:
    case CommandType::Status:
    case CommandType::Release:
      break;
  }

  if (fieldCount != expectedFieldCount) {
    return ParseResult::BadFieldCount;
  }

  command.argumentCount = fieldCount - HeaderFieldCount;
  for (std::size_t index = 0; index < command.argumentCount; ++index) {
    command.arguments[index] = fields[index + HeaderFieldCount];
  }

  return ParseResult::Ok;
}

Base64DecodeResult decodeBase64(
    const char* encoded, char* destination,
    std::size_t destinationSize) {
  if (destination == nullptr || destinationSize == 0) {
    return Base64DecodeResult::Invalid;
  }

  std::size_t outputLength = 0;
  const Base64DecodeResult result = decodeBase64Bytes(
      encoded, reinterpret_cast<std::uint8_t*>(destination),
      destinationSize - 1U, outputLength);
  if (result == Base64DecodeResult::Ok) {
    destination[outputLength] = '\0';
  }
  return result;
}

Base64DecodeResult decodeBase64Bytes(
    const char* encoded, std::uint8_t* destination,
    std::size_t destinationSize, std::size_t& outputLength) {
  outputLength = 0;
  if (encoded == nullptr || destination == nullptr) {
    return Base64DecodeResult::Invalid;
  }

  const std::size_t encodedLength = std::strlen(encoded);
  if (encodedLength % 4U != 0) {
    return Base64DecodeResult::Invalid;
  }

  for (std::size_t offset = 0; offset < encodedLength;
       offset += 4U) {
    const bool finalGroup = offset + 4U == encodedLength;
    const int first = decodeBase64Value(encoded[offset]);
    const int second = decodeBase64Value(encoded[offset + 1U]);
    if (first < 0 || second < 0) {
      return Base64DecodeResult::Invalid;
    }

    const bool thirdPadding = encoded[offset + 2U] == '=';
    const bool fourthPadding = encoded[offset + 3U] == '=';
    std::size_t decodedCount = 3;
    int third = 0;
    int fourth = 0;

    if (thirdPadding) {
      if (!fourthPadding || !finalGroup || (second & 0x0F) != 0) {
        return Base64DecodeResult::Invalid;
      }
      decodedCount = 1;
    } else {
      third = decodeBase64Value(encoded[offset + 2U]);
      if (third < 0) {
        return Base64DecodeResult::Invalid;
      }
      if (fourthPadding) {
        if (!finalGroup || (third & 0x03) != 0) {
          return Base64DecodeResult::Invalid;
        }
        decodedCount = 2;
      } else {
        fourth = decodeBase64Value(encoded[offset + 3U]);
        if (fourth < 0) {
          return Base64DecodeResult::Invalid;
        }
      }
    }

    if (outputLength + decodedCount > destinationSize) {
      return Base64DecodeResult::DestinationTooSmall;
    }

    destination[outputLength++] = static_cast<std::uint8_t>(
        (first << 2) | (second >> 4));
    if (decodedCount >= 2) {
      destination[outputLength++] = static_cast<std::uint8_t>(
          (second << 4) | (third >> 2));
    }
    if (decodedCount == 3) {
      destination[outputLength++] = static_cast<std::uint8_t>(
          (third << 6) | fourth);
    }
  }

  return Base64DecodeResult::Ok;
}

const char* errorCode(ParseResult result) {
  switch (result) {
    case ParseResult::Ok:
      return "OK";
    case ParseResult::NotProtocol:
      return "NOT_PROTOCOL";
    case ParseResult::UnsupportedVersion:
      return "UNSUPPORTED_VERSION";
    case ParseResult::BadFieldCount:
      return "BAD_FIELD_COUNT";
    case ParseResult::BadSequence:
      return "BAD_SEQUENCE";
    case ParseResult::UnknownCommand:
      return "UNKNOWN_COMMAND";
  }
  return "UNKNOWN_ERROR";
}

}  // namespace ProtocolParser

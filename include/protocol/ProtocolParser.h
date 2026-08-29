#pragma once

#include <cstddef>
#include <cstdint>

namespace ProtocolParser {

constexpr std::size_t MaxLineLength = 1400;
constexpr std::size_t MaxArgumentCount = 6;
constexpr std::uint16_t MaxSequence = 65535;

enum class CommandType : std::uint8_t {
  None,
  Hello,
  Ping,
  Status,
  Media,
  State,
  Release,
  CoverBegin,
  CoverData,
  CoverEnd,
  CoverStream,
  Karaoke,
  KaraokeClear,
};

enum class ParseResult : std::uint8_t {
  Ok,
  NotProtocol,
  UnsupportedVersion,
  BadFieldCount,
  BadSequence,
  UnknownCommand,
};

enum class Base64DecodeResult : std::uint8_t {
  Ok,
  Invalid,
  DestinationTooSmall,
};

struct Command {
  CommandType type = CommandType::None;
  std::uint8_t protocolVersion = 1;
  std::uint16_t sequence = 0;
  const char* arguments[MaxArgumentCount]{};
  std::size_t argumentCount = 0;
};

ParseResult parse(char* line, Command& command);
Base64DecodeResult decodeBase64(const char* encoded,
                                char* destination,
                                std::size_t destinationSize);
Base64DecodeResult decodeBase64Bytes(
    const char* encoded, std::uint8_t* destination,
    std::size_t destinationSize, std::size_t& outputLength);
const char* errorCode(ParseResult result);

}  // namespace ProtocolParser
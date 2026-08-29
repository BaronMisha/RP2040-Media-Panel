#pragma once

#include <cstdint>

namespace ProtocolSelfTest {

enum class Result : std::uint8_t {
  Passed,
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
  Base64,
  BinaryBase64,
  InvalidBase64,
  Base64Capacity,
  UnsupportedVersion,
  BadFieldCount,
  BadSequence,
  UnknownCommand,
};

Result run();
const char* resultName(Result result);

}  // namespace ProtocolSelfTest

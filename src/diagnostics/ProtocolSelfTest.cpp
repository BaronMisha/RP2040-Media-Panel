#include "diagnostics/ProtocolSelfTest.h"

#include <cstring>

#include "protocol/ProtocolParser.h"

namespace ProtocolSelfTest {
namespace {

bool parsesAs(char* line,
              ProtocolParser::CommandType expectedType,
              std::uint16_t expectedSequence) {
  ProtocolParser::Command command;
  return ProtocolParser::parse(line, command) ==
             ProtocolParser::ParseResult::Ok &&
         command.type == expectedType &&
         command.sequence == expectedSequence;
}

}  // namespace

Result run() {
  char hello[] = "@RPMP1|HELLO|1";
  if (!parsesAs(hello, ProtocolParser::CommandType::Hello, 1)) {
    return Result::Hello;
  }

  char ping[] = "@RPMP1|PING|65535";
  if (!parsesAs(ping, ProtocolParser::CommandType::Ping, 65535)) {
    return Result::Ping;
  }

  char status[] = "@RPMP1|STATUS|42";
  if (!parsesAs(status, ProtocolParser::CommandType::Status, 42)) {
    return Result::Status;
  }

  char media[] =
      "@RPMP1|MEDIA|7|99|60000|VGVzdA==|QXJ0aXN0|QWxidW0=";
  ProtocolParser::Command mediaCommand;
  if (ProtocolParser::parse(media, mediaCommand) !=
          ProtocolParser::ParseResult::Ok ||
      mediaCommand.type != ProtocolParser::CommandType::Media ||
      mediaCommand.sequence != 7 ||
      mediaCommand.argumentCount != 5 ||
      std::strcmp(mediaCommand.arguments[0], "99") != 0 ||
      std::strcmp(mediaCommand.arguments[4], "QWxidW0=") != 0) {
    return Result::Media;
  }

  char state[] = "@RPMP1|STATE|8|1250|60000|80|1";
  if (!parsesAs(state, ProtocolParser::CommandType::State, 8)) {
    return Result::State;
  }

  char release[] = "@RPMP1|RELEASE|9";
  if (!parsesAs(release, ProtocolParser::CommandType::Release, 9)) {
    return Result::Release;
  }

  char coverBegin[] =
      "@RPMP1|COVER_BEGIN|10|99|160|160|51200|305419896";
  ProtocolParser::Command coverBeginCommand;
  if (ProtocolParser::parse(coverBegin, coverBeginCommand) !=
          ProtocolParser::ParseResult::Ok ||
      coverBeginCommand.type !=
          ProtocolParser::CommandType::CoverBegin ||
      coverBeginCommand.argumentCount != 5 ||
      std::strcmp(coverBeginCommand.arguments[4], "305419896") != 0) {
    return Result::CoverBegin;
  }

  char coverData[] = "@RPMP1|COVER_DATA|11|99|0|AAEC";
  ProtocolParser::Command coverDataCommand;
  if (ProtocolParser::parse(coverData, coverDataCommand) !=
          ProtocolParser::ParseResult::Ok ||
      coverDataCommand.type !=
          ProtocolParser::CommandType::CoverData ||
      coverDataCommand.argumentCount != 3) {
    return Result::CoverData;
  }

  char coverEnd[] = "@RPMP1|COVER_END|12|99";
  if (!parsesAs(coverEnd, ProtocolParser::CommandType::CoverEnd,
                12)) {
    return Result::CoverEnd;
  }

  char coverStream[] =
      "@RPMP2|COVER_STREAM|15|99|160|160|51200|305419896";
  ProtocolParser::Command coverStreamCommand;
  if (ProtocolParser::parse(coverStream, coverStreamCommand) !=
          ProtocolParser::ParseResult::Ok ||
      coverStreamCommand.type !=
          ProtocolParser::CommandType::CoverStream ||
      coverStreamCommand.protocolVersion != 2 ||
      coverStreamCommand.argumentCount != 5) {
    return Result::CoverStream;
  }

  char helloV2[] = "@RPMP2|HELLO|16";
  ProtocolParser::Command helloV2Command;
  if (ProtocolParser::parse(helloV2, helloV2Command) !=
          ProtocolParser::ParseResult::Ok ||
      helloV2Command.type != ProtocolParser::CommandType::Hello ||
      helloV2Command.protocolVersion != 2) {
    return Result::CoverStream;
  }

  char karaoke[] =
      "@RPMP1|KARAOKE|13|99|1000|5000|UHJldmlvdXM=|"
      "Q3VycmVudA==|TmV4dA==";
  ProtocolParser::Command karaokeCommand;
  if (ProtocolParser::parse(karaoke, karaokeCommand) !=
          ProtocolParser::ParseResult::Ok ||
      karaokeCommand.type !=
          ProtocolParser::CommandType::Karaoke ||
      karaokeCommand.argumentCount != 6 ||
      std::strcmp(karaokeCommand.arguments[0], "99") != 0 ||
      std::strcmp(karaokeCommand.arguments[5],
                  "TmV4dA==") != 0) {
    return Result::Karaoke;
  }

  char karaokeClear[] = "@RPMP1|KARAOKE_CLEAR|14|99";
  if (!parsesAs(karaokeClear,
                ProtocolParser::CommandType::KaraokeClear,
                14)) {
    return Result::KaraokeClear;
  }

  char decoded[8];
  if (ProtocolParser::decodeBase64(
          "VGVzdA==", decoded, sizeof(decoded)) !=
          ProtocolParser::Base64DecodeResult::Ok ||
      std::strcmp(decoded, "Test") != 0) {
    return Result::Base64;
  }

  if (ProtocolParser::decodeBase64(
          "VGVzdA=", decoded, sizeof(decoded)) !=
      ProtocolParser::Base64DecodeResult::Invalid) {
    return Result::InvalidBase64;
  }

  char tooSmall[4];
  if (ProtocolParser::decodeBase64(
          "VGVzdA==", tooSmall, sizeof(tooSmall)) !=
      ProtocolParser::Base64DecodeResult::DestinationTooSmall) {
    return Result::Base64Capacity;
  }

  uint8_t binary[3];
  std::size_t binaryLength = 0;
  if (ProtocolParser::decodeBase64Bytes(
          "AAEC", binary, sizeof(binary), binaryLength) !=
          ProtocolParser::Base64DecodeResult::Ok ||
      binaryLength != sizeof(binary) || binary[0] != 0 ||
      binary[1] != 1 || binary[2] != 2) {
    return Result::BinaryBase64;
  }

  ProtocolParser::Command command;
  char version[] = "@RPMP3|PING|1";
  if (ProtocolParser::parse(version, command) !=
      ProtocolParser::ParseResult::UnsupportedVersion) {
    return Result::UnsupportedVersion;
  }

  char fields[] = "@RPMP1|PING|1|EXTRA";
  if (ProtocolParser::parse(fields, command) !=
      ProtocolParser::ParseResult::BadFieldCount) {
    return Result::BadFieldCount;
  }

  char sequence[] = "@RPMP1|PING|65536";
  if (ProtocolParser::parse(sequence, command) !=
      ProtocolParser::ParseResult::BadSequence) {
    return Result::BadSequence;
  }

  char unknown[] = "@RPMP1|TRACK|3";
  if (ProtocolParser::parse(unknown, command) !=
      ProtocolParser::ParseResult::UnknownCommand ||
      command.sequence != 3) {
    return Result::UnknownCommand;
  }

  return Result::Passed;
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Passed:
      return "PASS";
    case Result::Hello:
      return "HELLO";
    case Result::Ping:
      return "PING";
    case Result::Status:
      return "STATUS";
    case Result::Media:
      return "MEDIA";
    case Result::State:
      return "STATE";
    case Result::Release:
      return "RELEASE";
    case Result::CoverBegin:
      return "COVER_BEGIN";
    case Result::CoverData:
      return "COVER_DATA";
    case Result::CoverEnd:
      return "COVER_END";
    case Result::CoverStream:
      return "COVER_STREAM";
    case Result::Karaoke:
      return "KARAOKE";
    case Result::KaraokeClear:
      return "KARAOKE_CLEAR";
    case Result::Base64:
      return "base64";
    case Result::BinaryBase64:
      return "binary base64";
    case Result::InvalidBase64:
      return "invalid base64";
    case Result::Base64Capacity:
      return "base64 capacity";
    case Result::UnsupportedVersion:
      return "unsupported version";
    case Result::BadFieldCount:
      return "field count";
    case Result::BadSequence:
      return "sequence";
    case Result::UnknownCommand:
      return "unknown command";
  }
  return "unknown";
}

}  // namespace ProtocolSelfTest

/** @file test_protocol.cpp Validates parsing of short, empty, and unaligned replies. */

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/protocol/command.h>
#include <libfreenect2/protocol/response.h>

using libfreenect2::protocol::DepthCameraParamsResponse;
using libfreenect2::protocol::FirmwareVersionResponse;
using libfreenect2::protocol::ReadFirmwareVersionsCommand;
using libfreenect2::protocol::RgbCameraParamsResponse;
using libfreenect2::protocol::SerialNumberResponse;
using libfreenect2::protocol::Status0x090000Response;

TEST(ProtocolResponse, EmptyAndShortRepliesHaveSafeDefaults)
{
  const std::vector<unsigned char> empty;
  EXPECT_TRUE(SerialNumberResponse(empty).toString().empty());
  EXPECT_TRUE(FirmwareVersionResponse(empty).toString().empty());
  EXPECT_EQ(Status0x090000Response(empty).toNumber(), 0u);

  const std::vector<unsigned char> short_reply(1, 0);
  EXPECT_FLOAT_EQ(DepthCameraParamsResponse(short_reply).toIrCameraParams().fx, 0.0f);
  EXPECT_FLOAT_EQ(RgbCameraParamsResponse(short_reply).toColorCameraParams().fx, 0.0f);
}

TEST(ProtocolResponse, ParsesUnalignedStatusReply)
{
  const uint32_t expected = 0x12345678u;
  std::vector<unsigned char> storage(sizeof(expected) + 1, 0);
  std::memcpy(&storage[1], &expected, sizeof(expected));
  std::vector<unsigned char> reply(storage.begin() + 1, storage.end());
  EXPECT_EQ(Status0x090000Response(reply).toNumber(), expected);
}

TEST(ProtocolResponse, FormatsTheMainFirmwareVersion)
{
  std::vector<unsigned char> reply(4 * 4 * sizeof(uint32_t), 0);
  const uint32_t version[4] = { 0x00010002u, 3u, 4u, 0u };
  std::memcpy(&reply[3 * 4 * sizeof(uint32_t)], version, sizeof(version));
  EXPECT_EQ(FirmwareVersionResponse(reply).toString(), "1.2.3.4");
}

TEST(ProtocolCommand, CarriesSequenceAndResponseBounds)
{
  ReadFirmwareVersionsCommand command(42);
  EXPECT_EQ(command.sequence(), 42u);
  EXPECT_EQ(command.maxResponseLength(), 0x200u);
  EXPECT_EQ(command.minResponseLength(), 0x200u);
  EXPECT_NE(command.data(), static_cast<const uint8_t*>(NULL));
  EXPECT_GT(command.size(), 0u);
}

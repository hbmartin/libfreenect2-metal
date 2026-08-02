/** @file test_protocol.cpp Validates parsing of short, empty, and valid replies. */

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/protocol/command.h>
#include <libfreenect2/protocol/response.h>
#include <libfreenect2/usb/error.h>

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

TEST(ProtocolResponse, ParsesLittleEndianStatusReply)
{
  const std::vector<unsigned char> reply = {0x78, 0x56, 0x34, 0x12};
  EXPECT_EQ(Status0x090000Response(reply).toNumber(), 0x12345678u);
}

TEST(ProtocolResponse, FormatsTheMainFirmwareVersion)
{
  std::vector<unsigned char> reply(4 * 4 * sizeof(uint32_t), 0);
  const unsigned char version[] = {0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00,
                                   0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
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

TEST(UsbDiagnostics, ClassifiesAndFormatsLinkSpeeds)
{
  EXPECT_TRUE(libfreenect2::usb::isKnownSubSuperSpeed(LIBUSB_SPEED_LOW));
  EXPECT_TRUE(libfreenect2::usb::isKnownSubSuperSpeed(LIBUSB_SPEED_FULL));
  EXPECT_TRUE(libfreenect2::usb::isKnownSubSuperSpeed(LIBUSB_SPEED_HIGH));
  EXPECT_FALSE(libfreenect2::usb::isKnownSubSuperSpeed(LIBUSB_SPEED_UNKNOWN));
  EXPECT_FALSE(libfreenect2::usb::isKnownSubSuperSpeed(LIBUSB_SPEED_SUPER));

  EXPECT_FALSE(libfreenect2::usb::isSuperSpeedOrHigher(LIBUSB_SPEED_UNKNOWN));
  EXPECT_TRUE(libfreenect2::usb::isSuperSpeedOrHigher(LIBUSB_SPEED_SUPER));
  EXPECT_TRUE(libfreenect2::usb::isSuperSpeedOrHigher(LIBUSB_SPEED_SUPER + 1));

  EXPECT_EQ("High-Speed (480 Mb/s)", libfreenect2::usb::formatLibusbSpeed(LIBUSB_SPEED_HIGH));
  EXPECT_EQ("SuperSpeed (5 Gb/s)", libfreenect2::usb::formatLibusbSpeed(LIBUSB_SPEED_SUPER));
  EXPECT_EQ("unknown", libfreenect2::usb::formatLibusbSpeed(LIBUSB_SPEED_UNKNOWN));
  EXPECT_EQ("SuperSpeed or faster", libfreenect2::usb::formatLibusbSpeed(LIBUSB_SPEED_SUPER + 1));
}

TEST(UsbDiagnostics, ExplainsBusyInterfaceClaims)
{
  const std::string busy = libfreenect2::usb::formatInterfaceClaimError(LIBUSB_ERROR_BUSY);
  EXPECT_NE(std::string::npos, busy.find("another process or driver"));
  EXPECT_NE(std::string::npos, busy.find("LIBUSB_DEBUG=3"));

  const std::string access = libfreenect2::usb::formatInterfaceClaimError(LIBUSB_ERROR_ACCESS);
  EXPECT_EQ(std::string::npos, access.find("another process or driver"));
}

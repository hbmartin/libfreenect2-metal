/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2014 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses.
 */

/** @file test_rgb_stream_parser.cpp Byte-buffer tests for RgbPacketStreamParser:
 * one valid JPEG packet is extracted; malformed streams are rejected without
 * crashing. */

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/config.h>
#include <libfreenect2/rgb_packet_stream_parser.h>
#include <libfreenect2/rgb_packet_processor.h>
#include <libfreenect2/packet_processor.h>

using libfreenect2::RgbPacket;
using libfreenect2::RgbPacketStreamParser;

namespace
{

// Mirrors the (file-local) on-wire structs in rgb_packet_stream_parser.cpp.
LIBFREENECT2_PACK(struct RawRgbHeader {
  uint32_t sequence;
  uint32_t magic_header; // 'BBBB' 0x42424242
});

LIBFREENECT2_PACK(struct RgbFooter {
  uint32_t magic_header; // '9999' 0x39393939
  uint32_t sequence;
  uint32_t filler_length;
  uint32_t unknown1;
  uint32_t unknown2;
  uint32_t timestamp;
  float exposure;
  float gain;
  uint32_t magic_footer; // 'BBBB' 0x42424242
  uint32_t packet_size;
  float gamma;
  uint32_t unknown4[3];
});

class CapturingRgbProcessor : public libfreenect2::PacketProcessor<RgbPacket>
{
public:
  int count = 0;
  size_t last_jpeg_length = 0;
  uint32_t last_sequence = 0xffffffff;

  void process(const RgbPacket& packet) override
  {
    ++count;
    last_jpeg_length = packet.jpeg_buffer_length;
    last_sequence = packet.sequence;
  }
};

// Assemble a full RGB stream packet: header + jpeg + footer. `good_eoi` controls
// whether the jpeg ends with the 0xFF 0xD9 EOI marker; `packet_size_override`
// lets a test corrupt the size field.
std::vector<unsigned char> makePacket(uint32_t sequence, size_t jpeg_len, bool good_eoi = true,
                                      int packet_size_delta = 0)
{
  std::vector<unsigned char> jpeg(jpeg_len, 0x11);
  if (jpeg_len >= 2)
  {
    jpeg[0] = 0xff;
    jpeg[1] = 0xd8; // SOI
  }
  if (good_eoi && jpeg_len >= 2)
  {
    jpeg[jpeg_len - 2] = 0xff;
    jpeg[jpeg_len - 1] = 0xd9; // EOI
  }
  else if (jpeg_len >= 2)
  {
    jpeg[jpeg_len - 2] = 0x00;
    jpeg[jpeg_len - 1] = 0x00;
  }

  const size_t total = sizeof(RawRgbHeader) + jpeg_len + sizeof(RgbFooter);
  std::vector<unsigned char> buf(total, 0);

  RawRgbHeader header;
  header.sequence = sequence;
  header.magic_header = 0x42424242;
  std::memcpy(buf.data(), &header, sizeof(header));

  std::memcpy(buf.data() + sizeof(RawRgbHeader), jpeg.data(), jpeg_len);

  RgbFooter footer;
  std::memset(&footer, 0, sizeof(footer));
  footer.magic_header = 0x39393939;
  footer.sequence = sequence;
  footer.filler_length = 0;
  footer.timestamp = 4242;
  footer.exposure = 1.0f;
  footer.gain = 1.0f;
  footer.magic_footer = 0x42424242;
  footer.packet_size = static_cast<uint32_t>(total) + packet_size_delta;
  footer.gamma = 1.0f;
  std::memcpy(buf.data() + sizeof(RawRgbHeader) + jpeg_len, &footer, sizeof(footer));

  return buf;
}

} // namespace

TEST(RgbStreamParser, ExtractsValidJpegPacket)
{
  CapturingRgbProcessor proc;
  RgbPacketStreamParser parser;
  parser.setPacketProcessor(&proc);

  const size_t jpeg_len = 512;
  std::vector<unsigned char> pkt = makePacket(/*sequence=*/9, jpeg_len);
  parser.onDataReceived(pkt.data(), pkt.size());

  ASSERT_EQ(proc.count, 1);
  EXPECT_EQ(proc.last_sequence, 9u);
  EXPECT_EQ(proc.last_jpeg_length, jpeg_len);
}

TEST(RgbStreamParser, RejectsPacketWithoutEoiMarker)
{
  CapturingRgbProcessor proc;
  RgbPacketStreamParser parser;
  parser.setPacketProcessor(&proc);

  std::vector<unsigned char> pkt = makePacket(3, 512, /*good_eoi=*/false);
  parser.onDataReceived(pkt.data(), pkt.size());

  EXPECT_EQ(proc.count, 0);
}

TEST(RgbStreamParser, RejectsPacketWithMismatchedSize)
{
  CapturingRgbProcessor proc;
  RgbPacketStreamParser parser;
  parser.setPacketProcessor(&proc);

  std::vector<unsigned char> pkt = makePacket(3, 512, true, /*packet_size_delta=*/+1);
  parser.onDataReceived(pkt.data(), pkt.size());

  EXPECT_EQ(proc.count, 0);
}

TEST(RgbStreamParser, IgnoresTruncatedInputWithoutCrash)
{
  CapturingRgbProcessor proc;
  RgbPacketStreamParser parser;
  parser.setPacketProcessor(&proc);

  // Fewer bytes than a header + footer — must return early, no crash.
  std::vector<unsigned char> tiny(16, 0xaa);
  parser.onDataReceived(tiny.data(), tiny.size());

  EXPECT_EQ(proc.count, 0);
}

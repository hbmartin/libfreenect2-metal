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

/** @file test_depth_stream_parser.cpp Byte-buffer state-machine tests for
 * DepthPacketStreamParser, including regression guards for the subsequence /
 * offset hardening from PRs #3 and #5. */

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/depth_packet_stream_parser.h>
#include <libfreenect2/depth_packet_processor.h>
#include <libfreenect2/packet_processor.h>

using libfreenect2::DepthPacket;
using libfreenect2::DepthPacketStreamParser;
using libfreenect2::DepthSubPacketFooter;

namespace
{

const size_t kSingleImage = 512u * 424u * 11u / 8u; // one subframe payload

// Records how many complete depth packets the parser forwarded.
class CapturingDepthProcessor : public libfreenect2::PacketProcessor<DepthPacket>
{
public:
  int count = 0;
  uint32_t last_sequence = 0xffffffff;

  void process(const DepthPacket& packet) override
  {
    ++count;
    last_sequence = packet.sequence;
  }
};

// Build one subpacket: kSingleImage payload bytes followed by a footer. The
// caller can override the footer length / subsequence to exercise error paths.
std::vector<unsigned char> makeSubpacket(uint32_t sequence, uint32_t subsequence,
                                         uint32_t footer_length = kSingleImage,
                                         uint32_t footer_subsequence_field = 0xffffffff)
{
  std::vector<unsigned char> buf(kSingleImage + sizeof(DepthSubPacketFooter), 0);
  // deterministic payload
  for (size_t i = 0; i < kSingleImage; ++i)
    buf[i] = static_cast<unsigned char>((i + sequence + subsequence) & 0xff);

  DepthSubPacketFooter* f = reinterpret_cast<DepthSubPacketFooter*>(&buf[kSingleImage]);
  f->magic0 = 0;
  f->magic1 = 0;
  f->timestamp = 1000 + subsequence;
  f->sequence = sequence;
  f->subsequence =
      (footer_subsequence_field == 0xffffffff) ? subsequence : footer_subsequence_field;
  f->length = footer_length;
  return buf;
}

void feed(DepthPacketStreamParser& parser, std::vector<unsigned char>& buf)
{
  parser.onDataReceived(buf.data(), buf.size());
}

} // namespace

TEST(DepthStreamParser, AssemblesFullFrameAndForwardsOnce)
{
  CapturingDepthProcessor proc;
  DepthPacketStreamParser parser;
  parser.setPacketProcessor(&proc);

  // 10 complete subsequences of sequence 1 ...
  for (uint32_t s = 0; s < 10; ++s)
  {
    std::vector<unsigned char> sub = makeSubpacket(1, s);
    feed(parser, sub);
  }
  // ... then the first subpacket of sequence 2 flushes sequence 1.
  std::vector<unsigned char> flush = makeSubpacket(2, 0);
  feed(parser, flush);

  EXPECT_EQ(proc.count, 1);
  EXPECT_EQ(proc.last_sequence, 1u);
}

TEST(DepthStreamParser, RejectsOutOfRangeSubsequenceWithoutForwarding)
{
  CapturingDepthProcessor proc;
  DepthPacketStreamParser parser;
  parser.setPacketProcessor(&proc);

  // Corrupted footer claims subsequence 99 (>= 10). Must be dropped, not used
  // to index outside the front buffer. ASan/UBSan in CI catch any OOB here.
  std::vector<unsigned char> bad = makeSubpacket(1, 0, kSingleImage, 99);
  feed(parser, bad);

  // A large 32-bit subsequence that would overflow subsequence*length too.
  std::vector<unsigned char> overflow = makeSubpacket(1, 0, kSingleImage, 0x00400000u);
  feed(parser, overflow);

  EXPECT_EQ(proc.count, 0);
}

TEST(DepthStreamParser, RejectsLengthMismatchWithoutCrash)
{
  CapturingDepthProcessor proc;
  DepthPacketStreamParser parser;
  parser.setPacketProcessor(&proc);

  // Footer length disagrees with the assembled working-buffer length.
  std::vector<unsigned char> bad = makeSubpacket(1, 0, kSingleImage - 1);
  feed(parser, bad);

  EXPECT_EQ(proc.count, 0);
}

TEST(DepthStreamParser, HandlesZeroLengthResyncAndGarbage)
{
  CapturingDepthProcessor proc;
  DepthPacketStreamParser parser;
  parser.setPacketProcessor(&proc);

  // Zero-length transfer is the resync signal — must not crash.
  parser.onDataReceived(0, 0);

  // Random-sized garbage that never matches the footer boundary — must be
  // absorbed without a crash or forwarded packet.
  std::vector<unsigned char> junk(1234, 0xab);
  feed(parser, junk);
  std::vector<unsigned char> junk2(kSingleImage + 7, 0x5c);
  feed(parser, junk2);

  EXPECT_EQ(proc.count, 0);
}

/** Coverage-guided target for malformed and fragmented depth USB packets. */

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <libfreenect2/depth_packet_stream_parser.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (size > 512 * 424 * 11 / 8 + 64)
    return 0;

  libfreenect2::DepthPacketStreamParser parser;
  if (size == 0)
  {
    parser.onDataReceived(NULL, 0, 0);
    return 0;
  }

  const size_t stride = 1 + data[0];
  size_t offset = 1;
  while (offset < size)
  {
    const size_t length = std::min(stride, size - offset);
    parser.onDataReceived(const_cast<unsigned char*>(data + offset), length, 0);
    offset += length;
  }

  // Exercise the parser's explicit resynchronization path between inputs.
  parser.onDataReceived(NULL, 0, 0);
  return 0;
}

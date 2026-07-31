/** Coverage-guided target for binary command response decoding. */

#include <cstddef>
#include <cstdint>
#include <vector>

#include <libfreenect2/protocol/response.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if(size == 0 || size > 64 * 1024)
    return 0;

  const unsigned int response_type = data[0] % 6;
  const std::vector<unsigned char> reply(data + 1, data + size);
  switch(response_type)
  {
  case 0:
    (void)libfreenect2::protocol::SerialNumberResponse(reply).toString();
    break;
  case 1:
    (void)libfreenect2::protocol::FirmwareVersionResponse(reply).toString();
    break;
  case 2:
    (void)libfreenect2::protocol::Status0x090000Response(reply).toNumber();
    break;
  case 3:
    (void)libfreenect2::protocol::GenericResponse(reply).toString();
    break;
  case 4:
    (void)libfreenect2::protocol::RgbCameraParamsResponse(reply).toColorCameraParams();
    break;
  default:
    (void)libfreenect2::protocol::DepthCameraParamsResponse(reply).toIrCameraParams();
    break;
  }
  return 0;
}

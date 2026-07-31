/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2014 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */

/** @file packet_pipeline.cpp Packet pipeline implementation. */

#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/async_packet_processor.h>
#include <libfreenect2/data_callback.h>
#include <libfreenect2/rgb_packet_stream_parser.h>
#include <libfreenect2/rgb_decoder_fallback.h>
#include <libfreenect2/depth_packet_stream_parser.h>
#include <libfreenect2/logging.h>
#include <libfreenect2/protocol/response.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace libfreenect2
{

PacketPipelineConfig::PacketPipelineConfig() : rgb_decoder(Auto), allow_fallback(true)
{
}

class UnavailableRgbPacketProcessor : public RgbPacketProcessor
{
public:
  virtual bool good() { return false; }
  virtual const char *name() { return "Unavailable RGB decoder"; }
  virtual void process(const RgbPacket &) {}
};

#if defined(LIBFREENECT2_WITH_VAAPI_SUPPORT) && \
    defined(LIBFREENECT2_WITH_TURBOJPEG_SUPPORT)
static RgbPacketProcessor *withVaapiRuntimeFallback(VaapiRgbPacketProcessor *processor,
                                                    bool allow_fallback)
{
  if(allow_fallback)
    return new RgbDecoderFallback(processor, new TurboJpegRgbPacketProcessor());
  return processor;
}
#endif

static RgbPacketProcessor *turboJpegOrUnavailable()
{
#if defined(LIBFREENECT2_WITH_TURBOJPEG_SUPPORT)
  return new TurboJpegRgbPacketProcessor();
#else
  return new UnavailableRgbPacketProcessor();
#endif
}

static bool parseRgbDecoder(const std::string &text, PacketPipelineConfig::RgbDecoder &decoder)
{
  std::string normalized(text);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  if(normalized == "auto")
    decoder = PacketPipelineConfig::Auto;
  else if(normalized == "turbojpeg" || normalized == "turbo")
    decoder = PacketPipelineConfig::TurboJPEG;
  else if(normalized == "videotoolbox" || normalized == "vt")
    decoder = PacketPipelineConfig::VideoToolbox;
  else if(normalized == "vaapi")
    decoder = PacketPipelineConfig::VAAPI;
  else if(normalized == "tegrajpeg" || normalized == "tegra")
    decoder = PacketPipelineConfig::TegraJPEG;
  else
    return false;
  return true;
}

static PacketPipelineConfig resolveRgbConfig(const PacketPipelineConfig &config)
{
  PacketPipelineConfig resolved = config;
  if(config.rgb_decoder != PacketPipelineConfig::Auto)
    return resolved;

  const char *processor = std::getenv("LIBFREENECT2_RGB_PROCESSOR");
  if(processor != 0 && !parseRgbDecoder(processor, resolved.rgb_decoder))
  {
    LOG_WARNING << "ignoring invalid LIBFREENECT2_RGB_PROCESSOR='" << processor << "'";
    resolved.rgb_decoder = PacketPipelineConfig::Auto;
  }
  const char *vaapi_device = std::getenv("LIBFREENECT2_VAAPI_DEVICE");
  if(resolved.vaapi_device.empty() && vaapi_device != 0)
    resolved.vaapi_device = vaapi_device;
  return resolved;
}

static RgbPacketProcessor *getDefaultRgbPacketProcessor(const PacketPipelineConfig &config)
{
  (void)config;
#if defined(LIBFREENECT2_WITH_VT_SUPPORT)
  return new VTRgbPacketProcessor();
#elif defined(LIBFREENECT2_WITH_VAAPI_SUPPORT)
  VaapiRgbPacketProcessor *vaapi = new VaapiRgbPacketProcessor(config.vaapi_device);
  if (vaapi->good())
#if defined(LIBFREENECT2_WITH_TURBOJPEG_SUPPORT)
    return withVaapiRuntimeFallback(vaapi, config.allow_fallback);
#else
    return vaapi;
#endif
  else
    delete vaapi;
  return new TurboJpegRgbPacketProcessor();
#elif defined(LIBFREENECT2_WITH_TEGRAJPEG_SUPPORT)
  RgbPacketProcessor *tegra = new TegraJpegRgbPacketProcessor();
  if (tegra->good())
    return tegra;
  else
    delete tegra;
  return new TurboJpegRgbPacketProcessor();
#elif defined(LIBFREENECT2_WITH_TURBOJPEG_SUPPORT)
  return new TurboJpegRgbPacketProcessor();
#else
  #error No jpeg decoder is enabled
#endif
}

static RgbPacketProcessor *getConfiguredRgbPacketProcessor(const PacketPipelineConfig &config)
{
  const PacketPipelineConfig resolved = resolveRgbConfig(config);
  if(resolved.rgb_decoder == PacketPipelineConfig::Auto)
    return getDefaultRgbPacketProcessor(resolved);

  RgbPacketProcessor *processor = 0;
  switch(resolved.rgb_decoder)
  {
  case PacketPipelineConfig::TurboJPEG:
#if defined(LIBFREENECT2_WITH_TURBOJPEG_SUPPORT)
    processor = new TurboJpegRgbPacketProcessor();
#endif
    break;
  case PacketPipelineConfig::VideoToolbox:
#if defined(LIBFREENECT2_WITH_VT_SUPPORT)
    processor = new VTRgbPacketProcessor();
#endif
    break;
  case PacketPipelineConfig::VAAPI:
#if defined(LIBFREENECT2_WITH_VAAPI_SUPPORT)
    processor = new VaapiRgbPacketProcessor(resolved.vaapi_device);
#endif
    break;
  case PacketPipelineConfig::TegraJPEG:
#if defined(LIBFREENECT2_WITH_TEGRAJPEG_SUPPORT)
    processor = new TegraJpegRgbPacketProcessor();
#endif
    break;
  case PacketPipelineConfig::Auto:
    break;
  }

  if(processor != 0 && processor->good())
  {
#if defined(LIBFREENECT2_WITH_VAAPI_SUPPORT) && \
    defined(LIBFREENECT2_WITH_TURBOJPEG_SUPPORT)
    if(resolved.rgb_decoder == PacketPipelineConfig::VAAPI)
      return withVaapiRuntimeFallback(static_cast<VaapiRgbPacketProcessor *>(processor),
                                      resolved.allow_fallback);
#endif
    return processor;
  }
  delete processor;
  if(resolved.allow_fallback)
  {
    LOG_WARNING << "requested RGB decoder is unavailable; falling back to TurboJPEG";
    return turboJpegOrUnavailable();
  }
  LOG_ERROR << "requested RGB decoder is unavailable and fallback is disabled";
  return new UnavailableRgbPacketProcessor();
}

class PacketPipelineComponents
{
public:
  RgbPacketStreamParser *rgb_parser_;
  DepthPacketStreamParser *depth_parser_;

  RgbPacketProcessor *rgb_processor_;
  BaseRgbPacketProcessor *async_rgb_processor_;
  DepthPacketProcessor *depth_processor_;
  BaseDepthPacketProcessor *async_depth_processor_;

  ~PacketPipelineComponents();
  void initialize(RgbPacketProcessor *rgb, DepthPacketProcessor *depth);
};

void PacketPipelineComponents::initialize(RgbPacketProcessor *rgb, DepthPacketProcessor *depth)
{
  rgb_parser_ = new RgbPacketStreamParser();
  depth_parser_ = new DepthPacketStreamParser();

  rgb_processor_ = rgb;
  depth_processor_ = depth;

  async_rgb_processor_ = new AsyncPacketProcessor<RgbPacket>(rgb_processor_);
  async_depth_processor_ = new AsyncPacketProcessor<DepthPacket>(depth_processor_);

  rgb_parser_->setPacketProcessor(async_rgb_processor_);
  depth_parser_->setPacketProcessor(async_depth_processor_);
}

PacketPipelineComponents::~PacketPipelineComponents()
{
  // Parsers first: their destructors return held buffers to the async
  // processors' allocators, so those must still be alive.
  delete rgb_parser_;
  delete depth_parser_;
  delete async_rgb_processor_;
  delete async_depth_processor_;
  delete rgb_processor_;
  delete depth_processor_;
}

PacketPipeline::PacketPipeline(const std::string &name): name_(name), comp_(new PacketPipelineComponents()) {}

PacketPipeline::~PacketPipeline()
{
  delete comp_;
}

const std::string &PacketPipeline::getName() const
{
  return name_;
}

bool PacketPipeline::good() const
{
  const bool rgb_good = comp_->rgb_processor_ == NULL || comp_->rgb_processor_->good();
  const bool depth_good = comp_->depth_processor_ == NULL || comp_->depth_processor_->good();
  return rgb_good && depth_good;
}

PacketPipeline::PacketParser *PacketPipeline::getRgbPacketParser() const
{
  return comp_->rgb_parser_;
}

PacketPipeline::PacketParser *PacketPipeline::getIrPacketParser() const
{
  return comp_->depth_parser_;
}

RgbPacketProcessor *PacketPipeline::getRgbPacketProcessor() const
{
  return comp_->rgb_processor_;
}

DepthPacketProcessor *PacketPipeline::getDepthPacketProcessor() const
{
  return comp_->depth_processor_;
}

CpuPacketPipeline::CpuPacketPipeline() : CpuPacketPipeline(PacketPipelineConfig())
{
}

CpuPacketPipeline::CpuPacketPipeline(const PacketPipelineConfig &config) : PacketPipeline("cpu")
{
  comp_->initialize(getConfiguredRgbPacketProcessor(config), new CpuDepthPacketProcessor());
}

CpuPacketPipeline::~CpuPacketPipeline() { }

#ifdef LIBFREENECT2_WITH_OPENGL_SUPPORT
OpenGLPacketPipeline::OpenGLPacketPipeline(void *parent_opengl_context, bool debug)
  : OpenGLPacketPipeline(PacketPipelineConfig(), parent_opengl_context, debug)
{
}

OpenGLPacketPipeline::OpenGLPacketPipeline(const PacketPipelineConfig &config,
                                           void *parent_opengl_context, bool debug) : PacketPipeline("opengl"), parent_opengl_context_(parent_opengl_context), debug_(debug)
{
  comp_->initialize(getConfiguredRgbPacketProcessor(config), new OpenGLDepthPacketProcessor(parent_opengl_context_, debug_));
}

OpenGLPacketPipeline::~OpenGLPacketPipeline() { }
#endif // LIBFREENECT2_WITH_OPENGL_SUPPORT


#ifdef LIBFREENECT2_WITH_OPENCL_SUPPORT
OpenCLPacketPipeline::OpenCLPacketPipeline(const int deviceId)
  : OpenCLPacketPipeline(PacketPipelineConfig(), deviceId)
{
}

OpenCLPacketPipeline::OpenCLPacketPipeline(const PacketPipelineConfig &config, const int deviceId) : PacketPipeline("opencl"), deviceId(deviceId)
{
  comp_->initialize(getConfiguredRgbPacketProcessor(config), new OpenCLDepthPacketProcessor(deviceId));
}

OpenCLPacketPipeline::~OpenCLPacketPipeline() { }


OpenCLKdePacketPipeline::OpenCLKdePacketPipeline(const int deviceId)
  : OpenCLKdePacketPipeline(PacketPipelineConfig(), deviceId)
{
}

OpenCLKdePacketPipeline::OpenCLKdePacketPipeline(const PacketPipelineConfig &config, const int deviceId) : PacketPipeline("opencl_kde"), deviceId(deviceId)
{
  comp_->initialize(getConfiguredRgbPacketProcessor(config), new OpenCLKdeDepthPacketProcessor(deviceId));
}

OpenCLKdePacketPipeline::~OpenCLKdePacketPipeline() { }
#endif // LIBFREENECT2_WITH_OPENCL_SUPPORT

#ifdef LIBFREENECT2_WITH_CUDA_SUPPORT
CudaPacketPipeline::CudaPacketPipeline(const int device_id)
  : CudaPacketPipeline(PacketPipelineConfig(), device_id)
{
}

CudaPacketPipeline::CudaPacketPipeline(const PacketPipelineConfig &config, const int device_id) : PacketPipeline("cuda"), deviceId(device_id)
{
  comp_->initialize(getConfiguredRgbPacketProcessor(config), new CudaDepthPacketProcessor(device_id));
}

CudaPacketPipeline::~CudaPacketPipeline() { }

CudaKdePacketPipeline::CudaKdePacketPipeline(const int device_id)
  : CudaKdePacketPipeline(PacketPipelineConfig(), device_id)
{
}

CudaKdePacketPipeline::CudaKdePacketPipeline(const PacketPipelineConfig &config, const int device_id) : PacketPipeline("cuda_kde"), deviceId(device_id)
{
  comp_->initialize(getConfiguredRgbPacketProcessor(config), new CudaKdeDepthPacketProcessor(device_id));
}

CudaKdePacketPipeline::~CudaKdePacketPipeline() { }
#endif // LIBFREENECT2_WITH_CUDA_SUPPORT

#ifdef LIBFREENECT2_WITH_METAL_SUPPORT
MetalPacketPipeline::MetalPacketPipeline(const int deviceId)
  : MetalPacketPipeline(PacketPipelineConfig(), deviceId)
{
}

MetalPacketPipeline::MetalPacketPipeline(const PacketPipelineConfig &config, const int deviceId) : PacketPipeline("metal"), deviceId(deviceId)
{
  comp_->initialize(getConfiguredRgbPacketProcessor(config), new MetalDepthPacketProcessor(deviceId));
}

MetalPacketPipeline::~MetalPacketPipeline() { }
#endif // LIBFREENECT2_WITH_METAL_SUPPORT

DumpPacketPipeline::DumpPacketPipeline() : PacketPipeline("dump")
{
  RgbPacketProcessor *rgb = new DumpRgbPacketProcessor();
  DepthPacketProcessor *depth = new DumpDepthPacketProcessor();
  comp_->initialize(rgb, depth);
}

DumpPacketPipeline::~DumpPacketPipeline() {}

const unsigned char* DumpPacketPipeline::getDepthP0Tables(size_t* length) {
  *length = sizeof(libfreenect2::protocol::P0TablesResponse);
  return static_cast<DumpDepthPacketProcessor*>(getDepthPacketProcessor())->getP0Tables();
}

const float* DumpPacketPipeline::getDepthXTable(size_t* length) {
  *length = DepthPacketProcessor::TABLE_SIZE;
  return static_cast<DumpDepthPacketProcessor*>(getDepthPacketProcessor())->getXTable();
}

const float* DumpPacketPipeline::getDepthZTable(size_t* length) {
  *length = DepthPacketProcessor::TABLE_SIZE;
  return static_cast<DumpDepthPacketProcessor*>(getDepthPacketProcessor())->getZTable();
}

const short* DumpPacketPipeline::getDepthLookupTable(size_t* length) {
  *length = DepthPacketProcessor::LUT_SIZE;
  return static_cast<DumpDepthPacketProcessor*>(getDepthPacketProcessor())->getLookupTable();
}

} /* namespace libfreenect2 */

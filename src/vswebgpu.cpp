#include "vswebgpu.h"

#include <ctime>
#include <iostream>
#include <memory>

std::string_view toStdStringView(WGPUStringView wgpuStringView) {
  return wgpuStringView.data == nullptr         ? std::string_view()
         : wgpuStringView.length == WGPU_STRLEN ? std::string_view(wgpuStringView.data)
                                                : std::string_view(wgpuStringView.data, wgpuStringView.length);
}

WGPUStringView toWgpuStringView(std::string_view stdStringView) { return {stdStringView.data(), stdStringView.size()}; }

WGPUStringView toWgpuStringView(const char *cString) { return {cString, WGPU_STRLEN}; }

WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const *options) {
  struct UserData {
    WGPUAdapter adapter = nullptr;
    bool requestEnded = false;
  };
  UserData userData;

  auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView /* message */,
                                  void *userdata1, void * /* userdata2 */) {
    UserData &userData = *reinterpret_cast<UserData *>(userdata1);
    if (status == WGPURequestAdapterStatus_Success) {
      userData.adapter = adapter;
    } else {
      throw "Error while requesting adapter";
    }
    userData.requestEnded = true;
  };

  WGPURequestAdapterCallbackInfo callbackInfo = {nullptr, WGPUCallbackMode_AllowProcessEvents, onAdapterRequestEnded,
                                                 &userData, nullptr};

  wgpuInstanceRequestAdapter(instance, options, callbackInfo);

  wgpuInstanceProcessEvents(instance);
  while (!userData.requestEnded) {
    wgpuInstanceProcessEvents(instance);
  }

  return userData.adapter;
}

WGPUDevice requestDeviceSync(WGPUInstance instance, WGPUAdapter adapter, WGPUDeviceDescriptor const *descriptor) {
  struct UserData {
    WGPUDevice device = nullptr;
    bool requestEnded = false;
  };
  UserData userData;

  // The callback
  auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                                 void *userdata1, void * /* userdata2 */
                              ) {
    UserData &userData = *reinterpret_cast<UserData *>(userdata1);
    if (status == WGPURequestDeviceStatus_Success) {
      userData.device = device;
    } else {
      throw "Error while requesting device";
    }
    userData.requestEnded = true;
  };

  WGPURequestDeviceCallbackInfo callbackInfo = {/* nextInChain = */ nullptr,
                                                /* mode = */ WGPUCallbackMode_AllowProcessEvents,
                                                /* callback = */ onDeviceRequestEnded,
                                                /* userdata1 = */ &userData,
                                                /* userdata2 = */ nullptr};

  wgpuAdapterRequestDevice(adapter, descriptor, callbackInfo);

  wgpuInstanceProcessEvents(instance);
  while (!userData.requestEnded) {
    wgpuInstanceProcessEvents(instance);
  }

  return userData.device;
}

void Instance::print_limits() {
  {
    WGPULimits supportedLimits = {};
    supportedLimits.nextInChain = nullptr;

    bool success = wgpuAdapterGetLimits(adapter, &supportedLimits) == WGPUStatus_Success;

    if (success) {
      std::cout << "Adapter limits:" << std::endl;
      std::cout << " - maxTextureDimension1D: " << supportedLimits.maxTextureDimension1D << std::endl;
      std::cout << " - maxTextureDimension2D: " << supportedLimits.maxTextureDimension2D << std::endl;
      std::cout << " - maxTextureDimension3D: " << supportedLimits.maxTextureDimension3D << std::endl;
      std::cout << " - maxTextureArrayLayers: " << supportedLimits.maxTextureArrayLayers << std::endl;
    }
  }

  {
    WGPUAdapterInfo properties;
    properties.nextInChain = nullptr;
    wgpuAdapterGetInfo(adapter, &properties);
    std::cout << "Adapter properties:" << std::endl;
    std::cout << " - vendorID: " << properties.vendorID << std::endl;
    std::cout << " - vendorName: " << toStdStringView(properties.vendor) << std::endl;
    std::cout << " - architecture: " << toStdStringView(properties.architecture) << std::endl;
    std::cout << " - deviceID: " << properties.deviceID << std::endl;
    std::cout << " - name: " << toStdStringView(properties.device) << std::endl;
    std::cout << " - driverDescription: " << toStdStringView(properties.description) << std::endl;
    std::cout << std::hex;
    std::cout << " - adapterType: 0x" << properties.adapterType << std::endl;
    std::cout << " - backendType: 0x" << properties.backendType << std::endl;
    std::cout << std::dec; // Restore decimal numbers
    wgpuAdapterInfoFreeMembers(properties);
  }

  {
    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuDeviceGetFeatures(device, &features);
    std::cout << "Device features:" << std::endl;
    std::cout << std::hex;
    for (size_t i = 0; i < features.featureCount; ++i) {
      std::cout << " - 0x" << features.features[i] << std::endl;
    }
    std::cout << std::dec;
    wgpuSupportedFeaturesFreeMembers(features);

    WGPULimits limits = WGPU_LIMITS_INIT;
    bool success = wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success;

    if (success) {
      std::cout << "Device limits:" << std::endl;
      std::cout << " - maxTextureDimension1D: " << limits.maxTextureDimension1D << std::endl;
      std::cout << " - maxTextureDimension2D: " << limits.maxTextureDimension2D << std::endl;
      std::cout << " - maxTextureDimension3D: " << limits.maxTextureDimension3D << std::endl;
      std::cout << " - maxTextureArrayLayers: " << limits.maxTextureArrayLayers << std::endl;
    }
  }
}

static void VS_CC create_compute(const VSMap *in, VSMap *out, [[maybe_unused]] void *userData, VSCore *core,
                                 const VSAPI *vsapi) {
  auto d = std::make_unique<ComputeData>();

  d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
  d->instance = (Instance *)vsapi->mapGetInt(in, "device", 0, NULL);
  d->pipeline = (WGPUComputePipeline)vsapi->mapGetInt(in, "pipeline", 0, NULL);

  d->vi = vsapi->getVideoInfo(d->node);

  if (!vsh::isConstantVideoFormat(d->vi) ||
      (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample != 16)) {
    throw "Only 16 bit float input supported";
  }

  d->width = d->vi->width;
  d->height = d->vi->height;
  d->channels = 3;

  d->queue = wgpuDeviceGetQueue(d->instance->device);

  {
    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 1.0f;
    samplerDesc.compare = WGPUCompareFunction_Undefined;
    samplerDesc.maxAnisotropy = 1;
    d->sampler = wgpuDeviceCreateSampler(d->instance->device, &samplerDesc);
  }

  auto get_frame = [](int n, int activationReason, void *instanceData, [[maybe_unused]] void **frameData,
                      VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) -> const VSFrame * {
    auto d = static_cast<ComputeData *>(instanceData);

    if (activationReason == arInitial) {
      vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
      WGPUTexture texture0;
      WGPUTextureView texture0_view;
      WGPUTexture texture1;
      WGPUTextureView texture1_view;
      WGPUBuffer uniformBuffer;
      WGPUBuffer output_buffer;
      std::vector<uint16_t> data;
      data.resize(d->width * d->height * 4);

      {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 4 * 4;
        desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uniformBuffer = wgpuDeviceCreateBuffer(d->instance->device, &desc);
      }

      {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst |
                     WGPUTextureUsage_CopySrc;
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA16Float;
        desc.size = {d->width, d->height, 1};
        texture0 = wgpuDeviceCreateTexture(d->instance->device, &desc);
      }

      {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA16Float;
        desc.size = {d->width, d->height, 1};
        texture1 = wgpuDeviceCreateTexture(d->instance->device, &desc);
      }

      {
        WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        desc.aspect = WGPUTextureAspect_All;
        desc.baseArrayLayer = 0;
        desc.arrayLayerCount = 1;
        desc.baseMipLevel = 0;
        desc.mipLevelCount = 1;
        desc.dimension = WGPUTextureViewDimension_2D;
        desc.format = desc.format;
        texture0_view = wgpuTextureCreateView(texture0, &desc);
        texture1_view = wgpuTextureCreateView(texture1, &desc);
      }

      {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = d->width * d->height * 4 * sizeof(uint16_t);
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        output_buffer = wgpuDeviceCreateBuffer(d->instance->device, &desc);
      }

      auto src = vsapi->getFrameFilter(n, d->node, frameCtx);
      for (uint32_t c = 0; c < d->channels; c++) {
        const auto stride = vsapi->getStride(src, c) / d->vi->format.bytesPerSample;

        uint16_t *srcp = (uint16_t *)vsapi->getReadPtr(src, c);

        for (uint32_t y = 0; y < d->height; y++) {
          for (uint32_t x = 0; x < d->width; x++) {
            data[y * d->width * 4 + x * 4 + c] = srcp[y * stride + x];
          }
        }
      }

      {
        WGPUExtent3D size = {d->width, d->height, 1};

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = size.width * 4 * sizeof(uint16_t);
        layout.rowsPerImage = size.height;

        WGPUTexelCopyTextureInfo dest = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dest.texture = texture0;
        wgpuQueueWriteTexture(d->queue, &dest, data.data(), layout.bytesPerRow * layout.rowsPerImage, &layout, &size);
      }

      WGPUCommandEncoderDescriptor command_desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
      WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(d->instance->device, &command_desc);

      {
        std::vector<WGPUBindGroupEntry> bindGroupEntries;

        {
          WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
          entry.binding = 0;
          entry.textureView = texture0_view;
          bindGroupEntries.push_back(entry);
        }

        {
          WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
          entry.binding = 1;
          entry.textureView = texture1_view;
          bindGroupEntries.push_back(entry);
        }

        {
          WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
          entry.binding = 2;
          entry.sampler = d->sampler;
          bindGroupEntries.push_back(entry);
        }

        {
          WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
          entry.binding = 3;
          entry.buffer = uniformBuffer;
          bindGroupEntries.push_back(entry);
        }

        {
          float buf[4] = {n, 0, 0, 0};
          wgpuQueueWriteBuffer(d->queue, uniformBuffer, 0, buf, 16);
        }

        WGPUBindGroup group;
        {
          WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
          desc.entryCount = bindGroupEntries.size();
          desc.entries = bindGroupEntries.data();
          desc.layout = wgpuComputePipelineGetBindGroupLayout(d->pipeline, 0);

          group = wgpuDeviceCreateBindGroup(d->instance->device, &desc);
        }

        WGPUComputePassDescriptor desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &desc);

        wgpuComputePassEncoderSetPipeline(pass, d->pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (d->width + 15) / 16, (d->height + 15) / 16, 1);

        wgpuComputePassEncoderEnd(pass);
      }

      {
        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = texture1;

        WGPUTexelCopyBufferInfo dest = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dest.buffer = output_buffer;
        dest.layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        dest.layout.bytesPerRow = d->width * 4 * sizeof(uint16_t);
        dest.layout.rowsPerImage = d->height;
        WGPUExtent3D size = {d->width, d->height, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &dest, &size);
      }

      {
        WGPUCommandBufferDescriptor desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &desc);
        wgpuQueueSubmit(d->queue, 1, &command);
        wgpuCommandBufferRelease(command);
      }

      wgpuCommandEncoderRelease(encoder);

      decltype(src) fr[]{nullptr, nullptr, nullptr};
      constexpr int pl[]{0, 1, 2};
      auto dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src, core);

      WGPUBufferMapCallbackInfo info = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
      MapBufferData mapdata{false, output_buffer, data.size() * 2, [&](const uint16_t *buf) {
                              for (uint32_t c = 0; c < d->channels; c++) {
                                uint16_t *dstp = (uint16_t *)vsapi->getWritePtr(dst, c);
                                const auto stride = vsapi->getStride(dst, c) / d->vi->format.bytesPerSample;
                                for (uint32_t y = 0; y < d->height; y++) {
                                  for (uint32_t x = 0; x < d->width; x++) {
                                    dstp[y * stride + x] = buf[4 * y * d->width + x * 4 + c];
                                  }
                                }
                              }
                            }};

      info.userdata1 = &mapdata;
      info.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, WGPU_NULLABLE void *userdata1,
                         WGPU_NULLABLE void *userdata2) {
        MapBufferData *data = (MapBufferData *)userdata1;
        if (status == WGPUMapAsyncStatus_Success) {
          const uint16_t *output = (const uint16_t *)wgpuBufferGetConstMappedRange(data->buffer, 0, data->size);
          data->fn_read(output);
          wgpuBufferUnmap(data->buffer);
        }
        data->done = true;
      };

      wgpuBufferMapAsync(output_buffer, WGPUMapMode_Read, 0, mapdata.size, info);
      wgpuInstanceProcessEvents(d->instance->instance);
      while (!mapdata.done) {
        wgpuInstanceProcessEvents(d->instance->instance);
      }

      wgpuTextureViewRelease(texture0_view);
      wgpuTextureViewRelease(texture1_view);
      wgpuTextureDestroy(texture0);
      wgpuTextureDestroy(texture1);
      wgpuBufferDestroy(output_buffer);
      wgpuBufferDestroy(uniformBuffer);

      vsapi->freeFrame(src);
      return dst;
    }

    return nullptr;
  };

  auto free = [](void *instanceData, [[maybe_unused]] VSCore *core, const VSAPI *vsapi) {
    auto d = static_cast<ComputeData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
  };

  VSFilterDependency deps[]{{d->node, rpStrictSpatial}};
  vsapi->createVideoFilter(out, "WebGPU", d->vi, get_frame, free, fmParallel, deps, 1, d.get(), core);
  d.release();
}

static void VS_CC create_texture(const VSMap *in, VSMap *out, [[maybe_unused]] void *userData, VSCore *core,
                                 const VSAPI *vsapi) {
  auto d = std::make_unique<TextureData>();

  d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
  d->instance = (Instance *)vsapi->mapGetInt(in, "device", 0, NULL);

  d->vi = vsapi->getVideoInfo(d->node);

  if (!vsh::isConstantVideoFormat(d->vi) ||
      (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample != 16)) {
    throw "Only 16 bit float input supported";
  }

  d->width = d->vi->width;
  d->height = d->vi->height;
  d->channels = 3;
  d->buffer.resize(d->width * d->height * 4);

  {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst |
                 WGPUTextureUsage_CopySrc;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA16Float;
    desc.size = {d->width, d->height, 1};
    d->texture = wgpuDeviceCreateTexture(d->instance->device, &desc);
  }

  {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.aspect = WGPUTextureAspect_All;
    desc.baseArrayLayer = 0;
    desc.arrayLayerCount = 1;
    desc.baseMipLevel = 0;
    desc.mipLevelCount = 1;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.format = WGPUTextureFormat_RGBA16Float;
    d->texture_view = wgpuTextureCreateView(d->texture, &desc);
  }

  auto get_frame = [](int n, int activationReason, void *instanceData, [[maybe_unused]] void **frameData,
                      VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) -> const VSFrame * {
    auto d = static_cast<TextureData *>(instanceData);

    if (activationReason == arInitial) {
      vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
      auto src = vsapi->getFrameFilter(n, d->node, frameCtx);

      for (uint32_t c = 0; c < d->channels; c++) {
        const auto stride = vsapi->getStride(src, c) / d->vi->format.bytesPerSample;

        uint16_t *srcp = (uint16_t *)vsapi->getReadPtr(src, c);

        for (uint32_t y = 0; y < d->height; y++) {
          for (uint32_t x = 0; x < d->width; x++) {
            d->buffer[y * d->width * 4 + x * 4 + c] = srcp[y * stride + x];
          }
        }
      }

      WGPUQueue queue = wgpuDeviceGetQueue(d->instance->device);

      WGPUExtent3D size = {d->width, d->height, 1};

      WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
      layout.bytesPerRow = size.width * 4 * sizeof(uint16_t);
      layout.rowsPerImage = size.height;

      WGPUTexelCopyTextureInfo dest = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
      dest.texture = d->texture;
      wgpuQueueWriteTexture(queue, &dest, d->buffer.data(), layout.bytesPerRow * layout.rowsPerImage, &layout, &size);

      auto frame = vsapi->copyFrame(src, core);
      auto props = vsapi->getFramePropertiesRW(frame);
      vsapi->mapSetInt(props, "texture_view", (uint64_t)d->texture_view, 1);

      vsapi->freeFrame(src);
      return frame;
    }

    return nullptr;
  };

  auto free = [](void *instanceData, [[maybe_unused]] VSCore *core, const VSAPI *vsapi) {
    auto d = static_cast<TextureData *>(instanceData);
    wgpuTextureViewRelease(d->texture_view);
    wgpuTextureRelease(d->texture);
    delete d;
  };

  VSFilterDependency deps[]{{d->node, rpStrictSpatial}};
  vsapi->createVideoFilter(out, "WebGPU", d->vi, get_frame, free, fmParallelRequests, deps, 1, d.get(), core);
  d.release();
}

static void VS_CC create_device(const VSMap *in, VSMap *out, [[maybe_unused]] void *userData, VSCore *core,
                                const VSAPI *vsapi) {
  WGPUInstanceDescriptor desc = {};
  desc.nextInChain = nullptr;

  WGPUInstance instance = wgpuCreateInstance(&desc);

  if (!instance) {
    throw "Could not initialize WebGPU";
  }

  WGPURequestAdapterOptions adapterOpts = {};
  adapterOpts.nextInChain = nullptr;
  WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);

  WGPUDeviceDescriptor deviceDesc = WGPU_DEVICE_DESCRIPTOR_INIT;
  deviceDesc.nextInChain = nullptr;
  deviceDesc.requiredFeatureCount = 0;
  deviceDesc.requiredFeatures = nullptr;
  WGPULimits requiredLimits = WGPU_LIMITS_INIT;
  deviceDesc.requiredLimits = &requiredLimits;
  auto onDeviceError = [](WGPUDevice const *device, WGPUErrorType type, struct WGPUStringView message,
                          void * /* userdata1 */, void * /* userdata2 */
                       ) {
    std::cout << "Uncaptured error in device " << device << ": type " << type << " (" << toStdStringView(message) << ")"
              << std::endl;
  };

  deviceDesc.uncapturedErrorCallbackInfo.callback = onDeviceError;
  WGPUDevice device = requestDeviceSync(instance, adapter, &deviceDesc);

  auto wgpuInstance = std::make_unique<Instance>();
  wgpuInstance->adapter = adapter;
  wgpuInstance->instance = instance;
  wgpuInstance->device = device;
  wgpuInstance->print_limits();

  vsapi->mapSetInt(out, "device", (int64_t)wgpuInstance.get(), 0);
  wgpuInstance.release();
}

static void VS_CC create_pipeline(const VSMap *in, VSMap *out, [[maybe_unused]] void *userData, VSCore *core,
                                  const VSAPI *vsapi) {
  auto instance = (Instance *)vsapi->mapGetInt(in, "device", 0, NULL);
  WGPUDevice device = instance->device;
  const char *source = vsapi->mapGetData(in, "shader", 0, NULL);

  WGPUShaderSourceWGSL wgslSourceDesc = WGPU_SHADER_SOURCE_WGSL_INIT;
  wgslSourceDesc.code = toWgpuStringView(source);

  WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  moduleDesc.nextInChain = &wgslSourceDesc.chain;
  WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &moduleDesc);

  WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
  pipelineDesc.compute.module = shaderModule;
  WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

  wgpuShaderModuleRelease(shaderModule);

  vsapi->mapSetInt(out, "pipeline", (int64_t)pipeline, 0);
}

static void VS_CC create_buffer(const VSMap *in, VSMap *out, [[maybe_unused]] void *userData, VSCore *core,
                                const VSAPI *vsapi) {
  auto instance = (Instance *)vsapi->mapGetInt(in, "device", 0, NULL);
  WGPUDevice device = instance->device;
  int size = vsapi->mapGetInt(in, "size", 0, NULL);

  WGPUBufferDescriptor outputBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
  outputBufferDesc.size = size;
  outputBufferDesc.usage = WGPUBufferUsage_Storage;
  WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &outputBufferDesc);

  vsapi->mapSetInt(out, "buffer", (int64_t)buffer, 0);
}

VS_EXTERNAL_API(void)
VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
  vspapi->configPlugin("moe.grass.webgpu", "webgpu", "wgpu!", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0,
                       plugin);
  vspapi->registerFunction("Device", "", "device:int;", create_device, nullptr, plugin);
  vspapi->registerFunction("Pipeline",
                           "device:int;"
                           "shader:data",
                           "pipeline:int;", create_pipeline, nullptr, plugin);
  vspapi->registerFunction("Buffer",
                           "device:int;"
                           "size:int",
                           "buffer:int;", create_buffer, nullptr, plugin);
  vspapi->registerFunction("Texture",
                           "clip:vnode;"
                           "device:int;",
                           "clip:vnode;", create_texture, nullptr, plugin);
  vspapi->registerFunction("Compute",
                           "clip:vnode;"
                           "device:int;"
                           "pipeline:int;",
                           "clip:vnode;", create_compute, nullptr, plugin);
}

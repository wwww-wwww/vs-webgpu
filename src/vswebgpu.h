#pragma once

#include <functional>
#include <vector>

#include <VSHelper4.h>
#include <VapourSynth4.h>

#include <webgpu/webgpu.h>

class Instance {
public:
  WGPUAdapter adapter;
  WGPUInstance instance;
  WGPUDevice device;
  void print_limits();
};

struct MapBufferData {
  bool done;
  WGPUBuffer buffer;
  uint32_t size;
  std::function<void(const uint16_t *)> fn_read;
};

class TextureData {
public:
  ~TextureData() {
    wgpuTextureViewRelease(texture_view);
    wgpuTextureDestroy(texture);
  };

  VSNode *node;
  const VSVideoInfo *vi;

  Instance *instance;

  uint32_t width;
  uint32_t height;
  uint32_t channels;

  std::vector<uint16_t> buffer;
  WGPUTexture texture;
  WGPUTextureView texture_view;
};

class ComputeData {
public:
  ~ComputeData() {
    wgpuComputePipelineRelease(pipeline);
    wgpuQueueRelease(queue);
    wgpuSamplerRelease(sampler);
  };

  VSNode *node;
  const VSVideoInfo *vi;

  Instance *instance;

  WGPUComputePipeline pipeline;

  WGPUSampler sampler;

  WGPUQueue queue;

  uint32_t width;
  uint32_t height;
  uint32_t channels;
};

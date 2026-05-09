#pragma once

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
  bool done = false;
  WGPUBuffer buffer;
  uint8_t *dst;
  uint32_t size;
};

class ComputeData {
public:
  VSNode *node;
  const VSVideoInfo *vi;

  Instance *instance;
  WGPUBuffer uniformBuffer;

  WGPUComputePipeline pipeline;

  std::vector<uint8_t> data;

  uint32_t width;
  uint32_t height;
  uint32_t channels;
};

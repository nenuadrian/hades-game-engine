#include "webgpu_mesh_pipeline.hpp"

#include <cmath>
#include <cstring>

#include "../core/log.hpp"

namespace hades
{
  namespace
  {
    constexpr uint32_t kMaxLights = 16;
    constexpr uint32_t kDrawStride = 256;
    constexpr uint32_t kInitialDrawCapacity = 256;

    struct FrameUboData
    {
      float view[16];
      float proj[16];
      float cameraPos[4];
      float ambient[4];
      int32_t lightCount[4];
      float lightType[kMaxLights][4];
      float lightPosition[kMaxLights][4];
      float lightDirection[kMaxLights][4];
      float lightColor[kMaxLights][4];
      float lightParams[kMaxLights][4];
    };

    struct DrawUboData
    {
      float model[16];
      float baseColor[4];
      float metallicRoughness[4];
    };

    void writeMat4(float *dst, const math::Mat4 &m)
    {
      std::memcpy(dst, &m.m[0][0], sizeof(float) * 16);
    }

    // WGSL source mirroring mesh.vert.glsl + mesh.frag.glsl.
    // FrameData at group(0) binding(0); DrawData at group(1) binding(0) with
    // dynamic offsets (stride 256) for per-item data.
    const char *kShaderSource = R"WGSL(
struct FrameData {
  view: mat4x4<f32>,
  proj: mat4x4<f32>,
  cameraPos: vec4<f32>,
  ambient: vec4<f32>,
  lightCount: vec4<i32>,
  lightType: array<vec4<f32>, 16>,
  lightPosition: array<vec4<f32>, 16>,
  lightDirection: array<vec4<f32>, 16>,
  lightColor: array<vec4<f32>, 16>,
  lightParams: array<vec4<f32>, 16>,
};

struct DrawData {
  model: mat4x4<f32>,
  baseColor: vec4<f32>,
  metallicRoughness: vec4<f32>,
};

@group(0) @binding(0) var<uniform> frame: FrameData;
@group(1) @binding(0) var<uniform> draw: DrawData;

struct VsIn {
  @location(0) pos: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
};

struct VsOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) worldPos: vec3<f32>,
  @location(1) worldNormal: vec3<f32>,
  @location(2) uv: vec2<f32>,
};

@vertex
fn vs_main(in: VsIn) -> VsOut {
  var out: VsOut;
  let world = draw.model * vec4<f32>(in.pos, 1.0);
  out.worldPos = world.xyz;
  let n = mat3x3<f32>(draw.model[0].xyz, draw.model[1].xyz, draw.model[2].xyz) * in.normal;
  out.worldNormal = normalize(n);
  out.uv = in.uv;
  out.pos = frame.proj * frame.view * world;
  return out;
}

fn computeLight(i: i32, N: vec3<f32>, V: vec3<f32>, albedo: vec3<f32>, worldPos: vec3<f32>) -> vec3<f32> {
  let t = i32(frame.lightType[i].x);
  let lcol = frame.lightColor[i].rgb * frame.lightColor[i].a;

  var L: vec3<f32>;
  var atten: f32 = 1.0;
  if (t == 0) {
    L = normalize(-frame.lightDirection[i].xyz);
  } else {
    let toL = frame.lightPosition[i].xyz - worldPos;
    let dist = length(toL);
    L = toL / max(dist, 1e-4);
    let range = max(frame.lightParams[i].x, 1e-4);
    let d = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
    atten = d * d;
    if (t == 2) {
      let cosOuter = cos(frame.lightParams[i].z);
      let cosInner = cos(frame.lightParams[i].y);
      let cosAngle = dot(normalize(frame.lightDirection[i].xyz), -L);
      let spot = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
      atten = atten * spot;
    }
  }

  let NdotL = max(dot(N, L), 0.0);
  let H = normalize(L + V);
  let NdotH = max(dot(N, H), 0.0);

  let roughness = clamp(draw.metallicRoughness.y, 0.04, 1.0);
  let shininess = mix(64.0, 4.0, roughness);
  let spec = pow(NdotH, shininess) * (1.0 - roughness);
  let specColor = mix(vec3<f32>(0.04), albedo, draw.metallicRoughness.x);

  return atten * lcol * (albedo * NdotL + specColor * spec);
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
  let N = normalize(in.worldNormal);
  let V = normalize(frame.cameraPos.xyz - in.worldPos);
  let albedo = draw.baseColor.rgb;

  var col = frame.ambient.rgb * albedo;
  let n = min(frame.lightCount.x, 16);
  for (var i: i32 = 0; i < n; i = i + 1) {
    col = col + computeLight(i, N, V, albedo, in.worldPos);
  }

  return vec4<f32>(col, draw.baseColor.a);
}
)WGSL";
  }

  bool WebGPUMeshPipeline::init(const InitInfo &info)
  {
    if (initialized_)
      return true;
    if (info.device == nullptr || info.queue == nullptr)
      return false;

    device_ = info.device;
    queue_ = info.queue;
    colorFormat_ = info.colorFormat;
    depthFormat_ = info.depthFormat;
    drawStride_ = kDrawStride;

    // Shader module.
    WGPUShaderModuleWGSLDescriptor wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgsl.code = kShaderSource;
    WGPUShaderModuleDescriptor smDesc{};
    smDesc.nextInChain = &wgsl.chain;
    smDesc.label = "hades_mesh_shader";
    shaderModule_ = wgpuDeviceCreateShaderModule(device_, &smDesc);
    if (shaderModule_ == nullptr)
    {
      hades::Log::error("webgpu_mesh", "wgpuDeviceCreateShaderModule failed");
      return false;
    }

    // Frame bind group layout (group 0): single uniform buffer at binding 0.
    {
      WGPUBindGroupLayoutEntry e{};
      e.binding = 0;
      e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
      e.buffer.type = WGPUBufferBindingType_Uniform;
      e.buffer.minBindingSize = sizeof(FrameUboData);
      WGPUBindGroupLayoutDescriptor d{};
      d.entryCount = 1;
      d.entries = &e;
      frameBgl_ = wgpuDeviceCreateBindGroupLayout(device_, &d);
    }

    // Draw bind group layout (group 1): dynamic-offset uniform buffer at binding 0.
    {
      WGPUBindGroupLayoutEntry e{};
      e.binding = 0;
      e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
      e.buffer.type = WGPUBufferBindingType_Uniform;
      e.buffer.hasDynamicOffset = true;
      e.buffer.minBindingSize = sizeof(DrawUboData);
      WGPUBindGroupLayoutDescriptor d{};
      d.entryCount = 1;
      d.entries = &e;
      drawBgl_ = wgpuDeviceCreateBindGroupLayout(device_, &d);
    }

    // Pipeline layout.
    {
      WGPUBindGroupLayout bgls[2] = {frameBgl_, drawBgl_};
      WGPUPipelineLayoutDescriptor d{};
      d.bindGroupLayoutCount = 2;
      d.bindGroupLayouts = bgls;
      pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &d);
    }

    // Frame uniform buffer + bind group.
    {
      WGPUBufferDescriptor bd{};
      bd.size = sizeof(FrameUboData);
      bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
      frameUniformBuffer_ = wgpuDeviceCreateBuffer(device_, &bd);

      WGPUBindGroupEntry e{};
      e.binding = 0;
      e.buffer = frameUniformBuffer_;
      e.offset = 0;
      e.size = sizeof(FrameUboData);
      WGPUBindGroupDescriptor bgd{};
      bgd.layout = frameBgl_;
      bgd.entryCount = 1;
      bgd.entries = &e;
      frameBindGroup_ = wgpuDeviceCreateBindGroup(device_, &bgd);
    }

    // Draw uniform buffer + bind group (initial capacity, grown on demand).
    {
      drawCapacity_ = kInitialDrawCapacity;
      WGPUBufferDescriptor bd{};
      bd.size = static_cast<uint64_t>(drawStride_) * drawCapacity_;
      bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
      drawUniformBuffer_ = wgpuDeviceCreateBuffer(device_, &bd);

      WGPUBindGroupEntry e{};
      e.binding = 0;
      e.buffer = drawUniformBuffer_;
      e.offset = 0;
      e.size = sizeof(DrawUboData);
      WGPUBindGroupDescriptor bgd{};
      bgd.layout = drawBgl_;
      bgd.entryCount = 1;
      bgd.entries = &e;
      drawBindGroup_ = wgpuDeviceCreateBindGroup(device_, &bgd);
    }

    // Render pipeline.
    {
      WGPUVertexAttribute attrs[3]{};
      attrs[0].format = WGPUVertexFormat_Float32x3;
      attrs[0].offset = 0;
      attrs[0].shaderLocation = 0;
      attrs[1].format = WGPUVertexFormat_Float32x3;
      attrs[1].offset = sizeof(float) * 3;
      attrs[1].shaderLocation = 1;
      attrs[2].format = WGPUVertexFormat_Float32x2;
      attrs[2].offset = sizeof(float) * 6;
      attrs[2].shaderLocation = 2;

      WGPUVertexBufferLayout vbl{};
      vbl.arrayStride = sizeof(MeshVertex);
      vbl.stepMode = WGPUVertexStepMode_Vertex;
      vbl.attributeCount = 3;
      vbl.attributes = attrs;

      WGPUBlendState blend{};
      blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
      blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
      blend.color.operation = WGPUBlendOperation_Add;
      blend.alpha.srcFactor = WGPUBlendFactor_One;
      blend.alpha.dstFactor = WGPUBlendFactor_Zero;
      blend.alpha.operation = WGPUBlendOperation_Add;

      WGPUColorTargetState colorTarget{};
      colorTarget.format = colorFormat_;
      colorTarget.blend = &blend;
      colorTarget.writeMask = WGPUColorWriteMask_All;

      WGPUFragmentState fs{};
      fs.module = shaderModule_;
      fs.entryPoint = "fs_main";
      fs.targetCount = 1;
      fs.targets = &colorTarget;

      WGPUDepthStencilState ds{};
      ds.format = depthFormat_;
      ds.depthWriteEnabled = true;
      ds.depthCompare = WGPUCompareFunction_Less;
      ds.stencilFront.compare = WGPUCompareFunction_Always;
      ds.stencilFront.failOp = WGPUStencilOperation_Keep;
      ds.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
      ds.stencilFront.passOp = WGPUStencilOperation_Keep;
      ds.stencilBack = ds.stencilFront;
      ds.stencilReadMask = 0xFFFFFFFF;
      ds.stencilWriteMask = 0xFFFFFFFF;

      WGPURenderPipelineDescriptor pd{};
      pd.layout = pipelineLayout_;
      pd.vertex.module = shaderModule_;
      pd.vertex.entryPoint = "vs_main";
      pd.vertex.bufferCount = 1;
      pd.vertex.buffers = &vbl;
      pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
      pd.primitive.frontFace = WGPUFrontFace_CCW;
      pd.primitive.cullMode = WGPUCullMode_Back;
      pd.multisample.count = 1;
      pd.multisample.mask = 0xFFFFFFFF;
      pd.depthStencil = &ds;
      pd.fragment = &fs;

      pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pd);
      if (pipeline_ == nullptr)
      {
        hades::Log::error("webgpu_mesh", "wgpuDeviceCreateRenderPipeline failed");
        return false;
      }
    }

    initialized_ = true;
    return true;
  }

  void WebGPUMeshPipeline::destroy()
  {
    for (auto &m : meshes_)
      destroyMesh(m);
    meshReady_.fill(false);

    if (depthView_) { wgpuTextureViewRelease(depthView_); depthView_ = nullptr; }
    if (depthTexture_) { wgpuTextureDestroy(depthTexture_); wgpuTextureRelease(depthTexture_); depthTexture_ = nullptr; }
    depthWidth_ = 0;
    depthHeight_ = 0;

    if (drawBindGroup_) { wgpuBindGroupRelease(drawBindGroup_); drawBindGroup_ = nullptr; }
    if (drawUniformBuffer_) { wgpuBufferDestroy(drawUniformBuffer_); wgpuBufferRelease(drawUniformBuffer_); drawUniformBuffer_ = nullptr; }
    if (frameBindGroup_) { wgpuBindGroupRelease(frameBindGroup_); frameBindGroup_ = nullptr; }
    if (frameUniformBuffer_) { wgpuBufferDestroy(frameUniformBuffer_); wgpuBufferRelease(frameUniformBuffer_); frameUniformBuffer_ = nullptr; }

    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (pipelineLayout_) { wgpuPipelineLayoutRelease(pipelineLayout_); pipelineLayout_ = nullptr; }
    if (drawBgl_) { wgpuBindGroupLayoutRelease(drawBgl_); drawBgl_ = nullptr; }
    if (frameBgl_) { wgpuBindGroupLayoutRelease(frameBgl_); frameBgl_ = nullptr; }
    if (shaderModule_) { wgpuShaderModuleRelease(shaderModule_); shaderModule_ = nullptr; }

    device_ = nullptr;
    queue_ = nullptr;
    drawCapacity_ = 0;
    initialized_ = false;
  }

  WGPUTextureView WebGPUMeshPipeline::ensureDepth(uint32_t width, uint32_t height)
  {
    if (!initialized_ || width == 0 || height == 0)
      return nullptr;
    if (depthView_ != nullptr && width == depthWidth_ && height == depthHeight_)
      return depthView_;

    if (depthView_) { wgpuTextureViewRelease(depthView_); depthView_ = nullptr; }
    if (depthTexture_) { wgpuTextureDestroy(depthTexture_); wgpuTextureRelease(depthTexture_); depthTexture_ = nullptr; }

    WGPUTextureDescriptor td{};
    td.usage = WGPUTextureUsage_RenderAttachment;
    td.dimension = WGPUTextureDimension_2D;
    td.size = {width, height, 1};
    td.format = depthFormat_;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    depthTexture_ = wgpuDeviceCreateTexture(device_, &td);
    if (depthTexture_ == nullptr)
      return nullptr;

    WGPUTextureViewDescriptor vd{};
    vd.format = depthFormat_;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_DepthOnly;
    depthView_ = wgpuTextureCreateView(depthTexture_, &vd);

    depthWidth_ = width;
    depthHeight_ = height;
    return depthView_;
  }

  void WebGPUMeshPipeline::writeFrameUniforms(const RenderList &list)
  {
    FrameUboData ubo{};
    writeMat4(ubo.view, list.camera.view);
    // WebGPU clip Y is up (like GL), so no Y flip needed here.
    // Depth range is [0,1] — matches our math::Mat4 projection.
    writeMat4(ubo.proj, list.camera.projection);
    ubo.cameraPos[0] = list.camera.position.x;
    ubo.cameraPos[1] = list.camera.position.y;
    ubo.cameraPos[2] = list.camera.position.z;
    ubo.cameraPos[3] = 1.0f;
    ubo.ambient[0] = ubo.ambient[1] = ubo.ambient[2] = list.globalAmbient;
    ubo.ambient[3] = 1.0f;

    uint32_t lc = static_cast<uint32_t>(list.lights.size());
    if (lc > kMaxLights) lc = kMaxLights;
    ubo.lightCount[0] = static_cast<int32_t>(lc);
    for (uint32_t i = 0; i < lc; ++i)
    {
      const auto &L = list.lights[i];
      ubo.lightType[i][0] = static_cast<float>(L.type);
      ubo.lightPosition[i][0] = L.position.x;
      ubo.lightPosition[i][1] = L.position.y;
      ubo.lightPosition[i][2] = L.position.z;
      ubo.lightPosition[i][3] = 1.0f;
      ubo.lightDirection[i][0] = L.direction.x;
      ubo.lightDirection[i][1] = L.direction.y;
      ubo.lightDirection[i][2] = L.direction.z;
      ubo.lightColor[i][0] = L.colorR;
      ubo.lightColor[i][1] = L.colorG;
      ubo.lightColor[i][2] = L.colorB;
      ubo.lightColor[i][3] = L.intensity;
      ubo.lightParams[i][0] = L.range;
      ubo.lightParams[i][1] = L.innerConeAngle * 3.14159265f / 180.0f;
      ubo.lightParams[i][2] = L.outerConeAngle * 3.14159265f / 180.0f;
      ubo.lightParams[i][3] = L.ambientContribution;
    }

    wgpuQueueWriteBuffer(queue_, frameUniformBuffer_, 0, &ubo, sizeof(ubo));
  }

  void WebGPUMeshPipeline::writeDrawUniforms(const RenderList &list)
  {
    const size_t total = list.opaqueItems.size() + list.transparentItems.size();
    if (total == 0)
      return;

    // Grow draw buffer if needed.
    if (total > drawCapacity_)
    {
      if (drawBindGroup_) { wgpuBindGroupRelease(drawBindGroup_); drawBindGroup_ = nullptr; }
      if (drawUniformBuffer_) { wgpuBufferDestroy(drawUniformBuffer_); wgpuBufferRelease(drawUniformBuffer_); drawUniformBuffer_ = nullptr; }

      uint32_t newCap = drawCapacity_ > 0 ? drawCapacity_ : kInitialDrawCapacity;
      while (newCap < total) newCap *= 2;
      drawCapacity_ = newCap;

      WGPUBufferDescriptor bd{};
      bd.size = static_cast<uint64_t>(drawStride_) * drawCapacity_;
      bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
      drawUniformBuffer_ = wgpuDeviceCreateBuffer(device_, &bd);

      WGPUBindGroupEntry e{};
      e.binding = 0;
      e.buffer = drawUniformBuffer_;
      e.offset = 0;
      e.size = sizeof(DrawUboData);
      WGPUBindGroupDescriptor bgd{};
      bgd.layout = drawBgl_;
      bgd.entryCount = 1;
      bgd.entries = &e;
      drawBindGroup_ = wgpuDeviceCreateBindGroup(device_, &bgd);
    }

    // Pack one DrawUboData per stride slot and upload.
    std::vector<uint8_t> staging(static_cast<size_t>(drawStride_) * total, 0);
    size_t slot = 0;
    const auto writeItem = [&](const RenderItem &it) {
      DrawUboData d{};
      writeMat4(d.model, it.worldTransform);
      d.baseColor[0] = it.material.baseColorR;
      d.baseColor[1] = it.material.baseColorG;
      d.baseColor[2] = it.material.baseColorB;
      d.baseColor[3] = it.material.opacity;
      d.metallicRoughness[0] = it.material.metallic;
      d.metallicRoughness[1] = it.material.roughness;
      d.metallicRoughness[2] = 0.0f;
      d.metallicRoughness[3] = 0.0f;
      std::memcpy(staging.data() + slot * drawStride_, &d, sizeof(d));
      ++slot;
    };
    for (const auto &it : list.opaqueItems) writeItem(it);
    for (const auto &it : list.transparentItems) writeItem(it);

    wgpuQueueWriteBuffer(queue_, drawUniformBuffer_, 0, staging.data(), staging.size());
  }

  void WebGPUMeshPipeline::drawRenderList(
      WGPUCommandEncoder encoder,
      WGPUTextureView colorView,
      uint32_t width,
      uint32_t height,
      const RenderList &list)
  {
    if (!initialized_ || encoder == nullptr || colorView == nullptr ||
        width == 0 || height == 0)
      return;

    WGPUTextureView depth = ensureDepth(width, height);
    if (depth == nullptr)
      return;

    writeFrameUniforms(list);
    writeDrawUniforms(list);

    WGPURenderPassColorAttachment colorAttach{};
    colorAttach.view = colorView;
    colorAttach.loadOp = WGPULoadOp_Clear;
    colorAttach.storeOp = WGPUStoreOp_Store;
    colorAttach.clearValue = {0.1, 0.1, 0.1, 1.0};

    WGPURenderPassDepthStencilAttachment depthAttach{};
    depthAttach.view = depth;
    depthAttach.depthLoadOp = WGPULoadOp_Clear;
    depthAttach.depthStoreOp = WGPUStoreOp_Store;
    depthAttach.depthClearValue = 1.0f;
    depthAttach.depthReadOnly = false;
    depthAttach.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttach.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttach.stencilReadOnly = true;

    WGPURenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttach;
    passDesc.depthStencilAttachment = &depthAttach;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetViewport(
        pass, 0.0f, 0.0f,
        static_cast<float>(width), static_cast<float>(height),
        0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, width, height);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, frameBindGroup_, 0, nullptr);

    uint32_t slot = 0;
    const auto drawItems = [&](const std::vector<RenderItem> &items) {
      for (const auto &it : items)
      {
        auto &mesh = ensureMesh(it.primitiveType);
        if (mesh.indexCount == 0)
        {
          ++slot;
          continue;
        }

        uint32_t dynOffset = slot * drawStride_;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, drawBindGroup_, 1, &dynOffset);
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 0, mesh.vertexBuffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(
            pass, mesh.indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, mesh.indexCount, 1, 0, 0, 0);
        ++slot;
      }
    };

    drawItems(list.opaqueItems);
    drawItems(list.transparentItems);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
  }

  WebGPUMeshPipeline::GpuMesh &WebGPUMeshPipeline::ensureMesh(PrimitiveType type)
  {
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(meshes_.size()))
      idx = 0;
    if (!meshReady_[idx])
    {
      MeshCpuData cpu = buildPrimitiveMesh(type);
      if (!createMesh(cpu, meshes_[idx]))
      {
        hades::Log::error("webgpu_mesh", "failed to upload primitive mesh");
      }
      else
      {
        meshReady_[idx] = true;
      }
    }
    return meshes_[idx];
  }

  bool WebGPUMeshPipeline::createMesh(const MeshCpuData &src, GpuMesh &out)
  {
    if (src.vertices.empty() || src.indices.empty())
      return false;

    const uint64_t vbSize = sizeof(MeshVertex) * src.vertices.size();
    const uint64_t ibSize = sizeof(uint32_t) * src.indices.size();

    // WebGPU requires buffer sizes used for copies to be multiples of 4.
    // wgpuQueueWriteBuffer expects this for the data size.
    const uint64_t vbAligned = (vbSize + 3) & ~uint64_t{3};
    const uint64_t ibAligned = (ibSize + 3) & ~uint64_t{3};

    {
      WGPUBufferDescriptor bd{};
      bd.size = vbAligned;
      bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
      out.vertexBuffer = wgpuDeviceCreateBuffer(device_, &bd);
      if (out.vertexBuffer == nullptr)
        return false;
    }
    {
      WGPUBufferDescriptor bd{};
      bd.size = ibAligned;
      bd.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
      out.indexBuffer = wgpuDeviceCreateBuffer(device_, &bd);
      if (out.indexBuffer == nullptr)
        return false;
    }

    wgpuQueueWriteBuffer(queue_, out.vertexBuffer, 0, src.vertices.data(), vbSize);
    wgpuQueueWriteBuffer(queue_, out.indexBuffer, 0, src.indices.data(), ibSize);

    out.indexCount = static_cast<uint32_t>(src.indices.size());
    return true;
  }

  void WebGPUMeshPipeline::destroyMesh(GpuMesh &m)
  {
    if (m.vertexBuffer)
    {
      wgpuBufferDestroy(m.vertexBuffer);
      wgpuBufferRelease(m.vertexBuffer);
      m.vertexBuffer = nullptr;
    }
    if (m.indexBuffer)
    {
      wgpuBufferDestroy(m.indexBuffer);
      wgpuBufferRelease(m.indexBuffer);
      m.indexBuffer = nullptr;
    }
    m.indexCount = 0;
  }
}

# Metal 3 Adoption Assessment

**Question:** What speedups or other benefits could libfreenect2-metal gain from adopting
Metal 3 features (WWDC22 "Discover Metal 3": fast resource loading, offline compilation,
MetalFX upscaling, mesh shaders, ray tracing, ML acceleration)?

**Answer: Metal 3's headline features are almost entirely inapplicable to this workload.**
The Metal backend is a pure-compute depth pipeline — four small kernels over a 512×424
image — bound by synchronization structure and the 30 Hz USB stream, not by shader
throughput, asset I/O, or geometry. The real, measurable wins come from Metal best
practices that predate Metal 3 (listed at the end).

## Workload characteristics

The Metal backend (`src/metal_depth_packet_processor.mm` / `.metal`) runs four compute
kernels per depth frame:

```
processPixelStage1 → filterPixelStage1 → processPixelStage2 → filterPixelStage2
```

over a 512×424 image (~217k threads per dispatch). Key structural facts:

- **Shaders are AOT-compiled at build time** (`xcrun metal` → `.metallib`, embedded in the
  binary; see `CMakeLists.txt:438-502`), loaded via `newLibraryWithData`
  (`src/metal_depth_packet_processor.mm:342`).
- **All buffers use `MTLResourceStorageModeShared`** (unified memory).
- **Per-frame flow** (`run()`, `src/metal_depth_packet_processor.mm:559-681`): `memcpy`
  the USB packet in (line 581) → four compute encoders in one command buffer (lines
  594–657) → `commit` + `waitUntilCompleted` (line 661) → `memcpy` depth/IR out (lines
  676–677).
- **Color never touches Metal** — the 1920×1080 JPEG stream is decoded by
  TurboJPEG/VideoToolbox.

## Feature-by-feature applicability

| Metal 3 feature | Applicability | Why |
|---|---|---|
| Fast resource loading (`MTLIOCommandQueue`) | None | No disk asset streaming exists. The metallib is embedded in the binary; calibration tables arrive over USB; per-frame input is one small packet already in unified memory. |
| Offline compilation / `MTLBinaryArchive` | Marginal (init-only) | Shaders are already AOT-compiled to `.metallib`. The only remaining runtime cost is four compute-PSO specializations at `init()` (`buildPipelines`, `src/metal_depth_packet_processor.mm:420`) — tens of milliseconds, once per process, and OS-cached after the first run. A binary archive could shave first-launch init but changes nothing per-frame. |
| MetalFX upscaling | Not appropriate | Designed for rendered color images. Depth frames are metric measurements — a perceptual/temporal upscaler would hallucinate depth values. Output resolution (512×424) is fixed by the sensor anyway. |
| Mesh shaders | None | The backend has no raster/geometry pipeline at all; it is pure compute. (Only conceivably relevant to a point-cloud *viewer app*, which lives outside this library.) |
| Ray tracing improvements | None | No intersection/BVH workload exists. |
| ML acceleration (MPS / TensorFlow / PyTorch) | New scope, not a speedup | No ML stage exists. Could enable a *future* learned depth-denoising or hole-filling stage, but that is a feature addition, not an optimization of current code. |

## Where real performance is available (not Metal 3)

These are ordered by expected impact. All are Metal best practices available since well
before Metal 3.

1. **Drop the hard `waitUntilCompleted`**
   (`src/metal_depth_packet_processor.mm:661`). Use `addCompletedHandler` plus
   double-buffered resources so CPU packet prep for frame N+1 overlaps GPU execution of
   frame N. This is the single largest latency/throughput item: today the CPU thread
   blocks for the full GPU duration of every frame.

2. **One compute encoder instead of four** (`run()`,
   `src/metal_depth_packet_processor.mm:594-657`). Encode all four stages in a single
   `MTLComputeCommandEncoder` with
   `memoryBarrierWithScope:MTLBarrierScopeBuffers` between dependent dispatches. This
   cuts encoder setup/teardown overhead and lets the driver schedule the dispatches more
   tightly.

3. **Eliminate the two per-frame `memcpy`s**
   (`src/metal_depth_packet_processor.mm:581`, `676-677`). Back the output `Frame`
   objects directly with `[buf contents]` pointers (double-buffered so the GPU never
   writes a buffer the client still holds), and/or wrap the incoming packet with
   `newBufferWithBytesNoCopy`.

4. **Function constants** to specialize kernels for the bilateral/edge-filter
   configuration at PSO build time instead of runtime branches, removing per-pixel
   branching in the filter stages.

5. **Capability checks.** The code assumes unified memory. Add `hasUnifiedMemory` /
   `supportsFamily` gating, and consider private/managed storage on Intel Macs with
   discrete GPUs, where GPU reads of shared buffers cross PCIe.

## Recommendation

Do not adopt Metal 3 features — none of them address what this pipeline actually spends
time on. If depth-pipeline performance matters, scope a separate task for items 1–3
above (async completion, single encoder, zero-copy frames), which require runtime
verification on Apple Silicon hardware.

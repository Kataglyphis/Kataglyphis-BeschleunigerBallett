# WebGPU Demo (Rust, in your browser)

A live demo of the Rust `kataglyphis_webgpu_renderer` crate
(`ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer`),
compiled to WebAssembly and running on the WebGPU API — the same renderer
that runs natively on Vulkan/DX12/Metal. It renders a glTF scene with the
metallic-roughness PBR pipeline, directional shadow mapping (3×3 PCF), and
ACES tonemapping from an HDR target.

Requires a WebGPU-capable browser (Chrome/Edge 113+, Firefox 141+).

<a class="sd-btn sd-btn-primary" href="webgpu-demo/index.html" target="_blank" rel="noopener">
Open the demo full-page ↗
</a>

```{raw} html
<iframe
  src="webgpu-demo/index.html"
  title="Kataglyphis WebGPU glTF demo"
  style="width: 100%; aspect-ratio: 16 / 10; border: 1px solid #444; border-radius: 8px; margin-top: 1rem;"
  allow="fullscreen"
  loading="lazy"></iframe>
```

## About

- **Milestones shipped**: surface lifecycle, glTF loading (meshes,
  transforms, samplers, mipmaps, tangents), PBR materials (GGX
  metallic-roughness, normal/emissive/occlusion maps), directional shadows,
  HDR + ACES tonemap, this browser build.
- **Roadmap**: see `docs/webgpu-renderer-roadmap.md` in the repository —
  phases A–G from material correctness up to IBL, animation, and a
  Colosseum-scale showcase scene.
- **Rebuild the demo**: `cargo build -p kataglyphis_webgpu_renderer
  --target wasm32-unknown-unknown --release`, then `wasm-bindgen --target
  web` into `crates/webgpu_renderer/web/pkg`, and copy `web/` into
  `docs/source/_webgpu_demo/webgpu-demo/`.

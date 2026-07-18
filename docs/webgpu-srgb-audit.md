# WebGPU Renderer — sRGB/Linear Audit

Phase A closing item: every texture and attachment in
`crates/webgpu_renderer`, its color space, and why. Rule of thumb: *color*
data is sRGB-encoded and decoded by the hardware on sample; *data* maps and
all render math stay linear.

| Resource | Format | Space | Rationale |
| --- | --- | --- | --- |
| Base color texture | `Rgba8UnormSrgb` | sRGB | Authored color; hardware decode on sample |
| Emissive texture | `Rgba8UnormSrgb` | sRGB | Authored color |
| Normal map | `Rgba8Unorm` | linear | Vector data; `2n−1` decode in shader |
| Metallic-roughness | `Rgba8Unorm` | linear | Parameter data (B=metallic, G=roughness) |
| Occlusion | `Rgba8Unorm` | linear | Scalar data (R) |
| White / flat-normal fallbacks | `Rgba8Unorm` | linear | 255 encodes 1.0 identically in both spaces; normals must be linear |
| Mip generation | CPU box filter | mixed | sRGB textures averaged in linear space (decode→avg→encode); data maps averaged raw; alpha always linear |
| HDR scene target | `Rgba16Float` | linear | All lighting math is linear; values exceed 1.0 by design |
| Bloom chain (A/B) | `Rgba16Float` | linear | Operates on HDR energy above threshold |
| Shadow map | `Depth32Float` | n/a | Depth |
| Swapchain (native) | first sRGB format from surface caps | sRGB | Tonemap writes linear; hardware encodes |
| Swapchain (web) | browser-preferred (often `Bgra8Unorm`, non-sRGB) | caveat | WebGPU canvases don't offer sRGB formats; output is slightly dark on web. Known deviation — fix is a manual `linear_to_srgb` in the tonemap shader when the target is non-sRGB (roadmap refinement) |
| Headless readback target | `Rgba8UnormSrgb` | sRGB | Golden tests assert sRGB-encoded bytes (documented in `tests/headless.rs`) |
| egui overlay | surface format | matches target | egui-wgpu handles its own color management |

Single known deviation: the **web swapchain non-sRGB caveat** above.

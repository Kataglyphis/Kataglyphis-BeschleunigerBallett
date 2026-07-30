// Shared fullscreen-triangle vertex utility. Both renderers generate a
// single fullscreen triangle from the vertex index (no vertex buffer) with
// the same math: x = (vid/2)*4-1, y = (vid%2)*4-1. Centralising it removes
// the duplicated `vs_main` boilerplate and guarantees identical UV math.
//
// Usage in an entry point:
//   import fullscreen;
//   [shader("vertex")]
//   VsOut vs_main(uint vid : SV_VertexID) { return fullscreen_vs(vid); }

struct FullscreenVsOut
{
    float4 svPosition : SV_Position;
    float2 uv : TEXCOORD0;
};

// Generate a fullscreen triangle from the vertex index. UV origin is
// top-left (y flipped) to match texture sampling conventions.
FullscreenVsOut fullscreen_vs(uint vid)
{
    FullscreenVsOut o;
    float x = float(vid / 2) * 4.0 - 1.0;
    float y = float(vid % 2) * 4.0 - 1.0;
    o.svPosition = float4(x, y, 0.0, 1.0);
    o.uv = float2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);
    return o;
}
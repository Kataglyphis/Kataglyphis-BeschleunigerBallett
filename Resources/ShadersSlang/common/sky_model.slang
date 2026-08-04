// Shared analytic sky model: horizon/zenith/ground gradient, sun disk, and a
// diffuse hemisphere irradiance approximation over the same gradient.
//
// Single source of truth for three copies that had to agree because each
// feeds the others' ambient: sky/sky.slang's background render,
// forward.slang's analytic IBL fallback (reflected specular via
// hemisphere_irradiance + a caller-side sky_radiance wrapper), and the Rust
// CPU panorama in ibl.rs (EnvironmentImage::sky) that gets convolved into the
// prefiltered/irradiance maps replacing that same fallback.
//
// A combined "sky_radiance(dir, withSun)" is deliberately NOT provided here:
// the two shader callers source their light direction/intensity from
// different uniform blocks (sky.light_dir_intensity vs
// frame.light_dir_ambient + frame.light_color_intensity.w), so each keeps its
// own thin wrapper over sky_gradient + sun_disk.

static const float3 SKY_ZENITH = float3(0.09, 0.16, 0.35);
static const float3 SKY_HORIZON = float3(0.55, 0.62, 0.72);
static const float3 SKY_GROUND = float3(0.18, 0.16, 0.15);

// Horizon/zenith/ground gradient, no sun. dir: view or reflection direction (normalized).
float3 sky_gradient(float3 dir)
{
    if (dir.y >= 0.0)
        return lerp(SKY_HORIZON, SKY_ZENITH, pow(clamp(dir.y, 0.0, 1.0), 0.7));
    else
        return lerp(SKY_HORIZON, SKY_GROUND, clamp(-dir.y * 3.0, 0.0, 1.0));
}

// Analytic sun disk, additive on top of sky_gradient.
// dir: view or reflection direction, lightDir: direction towards the light (not required normalized), intensity: light intensity.
float3 sun_disk(float3 dir, float3 lightDir, float intensity)
{
    float3 l = normalize(lightDir);
    float cosSun = max(dot(dir, l), 0.0);
    float sun = pow(cosSun, 1200.0) * 24.0 + pow(cosSun, 48.0) * 0.5;
    return float3(1.0, 0.95, 0.85) * sun * (intensity * 0.4);
}

// Diffuse irradiance over the hemisphere around a surface normal, from the same gradient.
float3 hemisphere_irradiance(float3 n)
{
    float3 sky = lerp(SKY_HORIZON, SKY_ZENITH, pow(clamp(n.y, 0.0, 1.0), 0.7));
    return lerp(SKY_GROUND * 0.7, sky, clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
}

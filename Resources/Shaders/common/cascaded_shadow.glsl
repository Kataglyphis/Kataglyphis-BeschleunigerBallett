// Cascaded shadow sampling shared by the forward and deferred lighting
// shaders. Requires, declared BEFORE this include:
//   - uniform _SceneUBO { SceneUBO sceneUBO; }
//   - uniform sampler2DArray directional_shadow_maps
// Fraction of direct light blocked at this fragment: 0 = fully lit,
// 1 = fully shadowed. Cascade chosen by camera distance; 0..1 depth
// (GLM_FORCE_DEPTH_ZERO_TO_ONE project-wide).
float calc_cascaded_shadow(vec3 world_pos, vec3 N, vec3 L) {
    int cascade_count = int(sceneUBO.numCascades);
    if (cascade_count <= 0) { return 0.0; }

    // Cascades are split along VIEW DEPTH, so select with depth along the
    // camera forward axis. Radial distance is larger off-axis and pushed
    // fragments into a cascade whose light-space box does not contain them,
    // so they projected outside the shadow map and were treated as unlit.
    float frag_distance = dot(world_pos - sceneUBO.cam_pos.xyz, normalize(sceneUBO.view_dir.xyz));
    int cascade_index = cascade_count - 1;
    for (int i = 0; i < cascade_count; i++) {
        if (frag_distance < sceneUBO.cascadeSplits[i]) {
            cascade_index = i;
            break;
        }
    }

    vec4 light_space_pos = sceneUBO.cascadeLightSpaceMatrices[cascade_index] * vec4(world_pos, 1.0);
    vec3 proj = light_space_pos.xyz / light_space_pos.w;
    vec2 shadow_uv = proj.xy * 0.5 + 0.5;
    float current_depth = proj.z;
    if (current_depth > 1.0 || current_depth < 0.0
        || shadow_uv.x < 0.0 || shadow_uv.x > 1.0 || shadow_uv.y < 0.0 || shadow_uv.y > 1.0) {
        return 0.0;
    }

    float bias = max(0.002 * (1.0 - dot(N, L)), 0.0005);
    int radius = int(sceneUBO.pcfRadius);
    vec2 texel = 1.0 / vec2(textureSize(directional_shadow_maps, 0).xy);
    float occluded = 0.0;
    float taps = 0.0;
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            float map_depth = texture(directional_shadow_maps,
                vec3(shadow_uv + vec2(x, y) * texel, float(cascade_index))).r;
            occluded += (current_depth - bias) > map_depth ? 1.0 : 0.0;
            taps += 1.0;
        }
    }
    return occluded / max(taps, 1.0);
}

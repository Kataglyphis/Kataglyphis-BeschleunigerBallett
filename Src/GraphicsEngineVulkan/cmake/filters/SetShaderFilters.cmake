# setting all shader filters
# ---- SHADER FILTER  --- BEGIN
kataglyphis_append_prefixed_files(
  RASTER_SHADER_FILTER
  "${SHADER_SRC_DIR}rasterizer/"
  shader.vert
  shader.frag)

kataglyphis_append_prefixed_files(
  RAYTRACING_SHADER_FILTER
  "${SHADER_SRC_DIR}raytracing/"
  raytrace.rchit
  raytrace.rgen
  raytrace.rmiss
  shadow.rmiss)

kataglyphis_append_prefixed_files(
  COMMON_SHADER_FILTER
  "${SHADER_SRC_DIR}common/"
  Matlib.glsl
  raycommon.glsl
  ShadingLibrary.glsl)

kataglyphis_append_prefixed_files(
  POST_SHADER_FILTER
  "${SHADER_SRC_DIR}post/"
  post.vert
  post.frag)

kataglyphis_append_prefixed_files(
  PATH_TRACING_SHADER_FILTER
  "${SHADER_SRC_DIR}path_tracing/"
  path_tracing.comp)

kataglyphis_append_prefixed_files(
  COMPUTE_SHADER_FILTER
  "${SHADER_SRC_DIR}compute/"
  clouds.comp
  noise.comp)

kataglyphis_append_prefixed_files(
  PBR_SHADER_FILTER
  "${SHADER_SRC_DIR}pbr/"
  microfacet.glsl)

kataglyphis_append_prefixed_files(
  BRDF_SHADER_FILTER
  "${SHADER_SRC_DIR}pbr/brdf/"
  disney.glsl
  frostbite.glsl
  pbrBook.glsl
  phong.glsl
  unreal4.glsl)

kataglyphis_append_prefixed_files(
  SHADER_HOST_DEVICE_FILTER
  "${SHADER_SRC_DIR}hostDevice/"
  host_device_shared_vars.hpp)

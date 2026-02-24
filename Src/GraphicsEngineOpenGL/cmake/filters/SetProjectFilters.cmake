# setting all project filters
# ---- PROJECT FILTER  --- BEGIN
# ---- GUI FILTER  --- BEGIN
set(PROJECT_GUI_SRC_DIR ${PROJECT_SRC_DIR}gui/)
set(GUI_FILTER ${GUI_FILTER} ${PROJECT_GUI_SRC_DIR}GUI.cpp ${PROJECT_GUI_SRC_DIR}GUI.ixx)
# ---- GUI FILTER  --- END

# ---- COMPUTE FILTER  --- BEGIN
set(PROJECT_COMPUTE_SRC_DIR ${PROJECT_SRC_DIR}compute/)
set(COMPUTE_FILTER ${COMPUTE_FILTER} ${PROJECT_COMPUTE_SRC_DIR}ComputeShaderProgram.cpp
                   ${PROJECT_COMPUTE_SRC_DIR}ComputeShaderProgram.ixx)
# ---- COMPUTE FILTER  --- END

# ---- CLOUD FILTER  --- BEGIN
set(PROJECT_CLOUD_SRC_DIR ${PROJECT_SRC_DIR}scene/atmospheric_effects/clouds/)
set(CLOUD_FILTER
    ${CLOUD_FILTER}
    ${PROJECT_CLOUD_SRC_DIR}Clouds.cpp
    ${PROJECT_CLOUD_SRC_DIR}Clouds.ixx
    ${PROJECT_CLOUD_SRC_DIR}Noise.cpp
    ${PROJECT_CLOUD_SRC_DIR}Noise.ixx)
# ---- CLOUD FILTER  --- END

# ---- CAMERA FILTER  --- BEGIN
set(PROJECT_CAMERA_SRC_DIR ${PROJECT_SRC_DIR}camera/)
set(CAMERA_FILTER ${CAMERA_FILTER} ${PROJECT_CAMERA_SRC_DIR}Camera.cpp ${PROJECT_CAMERA_SRC_DIR}Camera.ixx)
# ---- CAMERA FILTER  --- END

# ---- RENDERER FILTER  --- BEGIN
set(PROJECT_RENDERER_SRC_DIR ${PROJECT_SRC_DIR}renderer/)
set(RENDERER_FILTER
    ${RENDERER_FILTER}
    ${PROJECT_RENDERER_SRC_DIR}Renderer.cpp
    ${PROJECT_RENDERER_SRC_DIR}Renderer.ixx
    ${PROJECT_RENDERER_SRC_DIR}RenderPassSceneDependend.cpp
    ${PROJECT_RENDERER_SRC_DIR}RenderPassSceneDependend.ixx
    ${PROJECT_RENDERER_SRC_DIR}ShaderIncludes.cpp
    ${PROJECT_RENDERER_SRC_DIR}ShaderIncludes.ixx
    ${PROJECT_RENDERER_SRC_DIR}ShaderProgram.cpp
    ${PROJECT_RENDERER_SRC_DIR}ShaderProgram.ixx
    ${PROJECT_RENDERER_SRC_DIR}OpenGLRendererConfig.hpp
    ${PROJECT_RENDERER_SRC_DIR}deferred/RenderPass.ixx
    ${PROJECT_RENDERER_SRC_DIR}deferred/GeometryPass.ixx
    ${PROJECT_RENDERER_SRC_DIR}deferred/GeometryPass.cpp
    ${PROJECT_RENDERER_SRC_DIR}deferred/LightingPass.ixx
    ${PROJECT_RENDERER_SRC_DIR}deferred/LightingPass.cpp
    ${PROJECT_RENDERER_SRC_DIR}deferred/GeometryPassShaderProgram.ixx
    ${PROJECT_RENDERER_SRC_DIR}deferred/GeometryPassShaderProgram.cpp
    ${PROJECT_RENDERER_SRC_DIR}deferred/LightingPassShaderProgram.ixx
    ${PROJECT_RENDERER_SRC_DIR}deferred/LightingPassShaderProgram.cpp
    ${PROJECT_RENDERER_SRC_DIR}deferred/GBuffer.ixx
    ${PROJECT_RENDERER_SRC_DIR}deferred/GBuffer.cpp)
# ---- RENDERER FILTER  --- END

# ---- LIGHT FILTER  --- BEGIN
set(PROJECT_LIGHT_SRC_DIR ${PROJECT_SRC_DIR}scene/light/)
set(LIGHT_FILTER ${LIGHT_FILTER} ${PROJECT_LIGHT_SRC_DIR}Light.cpp ${PROJECT_LIGHT_SRC_DIR}Light.ixx)
# ---- LIGHT FILTER  --- END

# ---- D_LIGHT FILTER  --- BEGIN
set(PROJECT_D_LIGHT_SRC_DIR ${PROJECT_SRC_DIR}scene/light/directional_light/)
set(D_LIGHT_FILTER
    ${D_LIGHT_FILTER}
    ${PROJECT_D_LIGHT_SRC_DIR}CascadedShadowMap.cpp
    ${PROJECT_D_LIGHT_SRC_DIR}CascadedShadowMap.ixx
    ${PROJECT_D_LIGHT_SRC_DIR}DirectionalLight.cpp
    ${PROJECT_D_LIGHT_SRC_DIR}DirectionalLight.ixx
    ${PROJECT_D_LIGHT_SRC_DIR}DirectionalShadowMapPass.cpp
    ${PROJECT_D_LIGHT_SRC_DIR}DirectionalShadowMapPass.ixx)
# ---- D_LIGHT FILTER  --- END

# ---- P_LIGHT FILTER  --- BEGIN
set(PROJECT_P_LIGHT_SRC_DIR ${PROJECT_SRC_DIR}scene/light/point_light/)
set(P_LIGHT_FILTER
    ${P_LIGHT_FILTER}
    ${PROJECT_P_LIGHT_SRC_DIR}OmniDirShadowMap.cpp
    ${PROJECT_P_LIGHT_SRC_DIR}OmniDirShadowMap.ixx
    ${PROJECT_P_LIGHT_SRC_DIR}OmniDirShadowShaderProgram.cpp
    ${PROJECT_P_LIGHT_SRC_DIR}OmniDirShadowShaderProgram.ixx
    ${PROJECT_P_LIGHT_SRC_DIR}OmniShadowMapPass.cpp
    ${PROJECT_P_LIGHT_SRC_DIR}OmniShadowMapPass.ixx
    ${PROJECT_P_LIGHT_SRC_DIR}PointLight.cpp
    ${PROJECT_P_LIGHT_SRC_DIR}PointLight.ixx)
# ---- P_LIGHT FILTER  --- END

# ---- SHADOWS FILTER  --- BEGIN
set(PROJECT_SHADOWS_SRC_DIR ${PROJECT_SRC_DIR}scene/shadows/)
set(SHADOWS_FILTER ${SHADOWS_FILTER} ${PROJECT_SHADOWS_SRC_DIR}ShadowMap.cpp ${PROJECT_SHADOWS_SRC_DIR}ShadowMap.ixx)
# ---- SHADOWS FILTER  --- END

# ---- SKY_BOX FILTER  --- BEGIN
set(PROJECT_SKY_BOX_SRC_DIR ${PROJECT_SRC_DIR}scene/sky_box/)
set(SKY_BOX_FILTER ${SKY_BOX_FILTER} ${PROJECT_SKY_BOX_SRC_DIR}SkyBox.cpp ${PROJECT_SKY_BOX_SRC_DIR}SkyBox.ixx)
# ---- SKY_BOX FILTER  --- END

# ---- TEXTURE FILTER  --- BEGIN
set(PROJECT_TEXTURE_SRC_DIR ${PROJECT_SRC_DIR}scene/texture/)
set(TEXTURE_FILTER
    ${TEXTURE_FILTER}
    ${PROJECT_TEXTURE_SRC_DIR}Texture.cpp
    ${PROJECT_TEXTURE_SRC_DIR}Texture.ixx
    ${PROJECT_TEXTURE_SRC_DIR}ClampToEdgeMode.cpp
    ${PROJECT_TEXTURE_SRC_DIR}ClampToEdgeMode.ixx
    ${PROJECT_TEXTURE_SRC_DIR}MirroredRepeatMode.cpp
    ${PROJECT_TEXTURE_SRC_DIR}MirroredRepeatMode.ixx
    ${PROJECT_TEXTURE_SRC_DIR}RepeatMode.cpp
    ${PROJECT_TEXTURE_SRC_DIR}RepeatMode.ixx
    ${PROJECT_TEXTURE_SRC_DIR}TextureWrappingMode.ixx)
# ---- TEXTURE FILTER  --- END

# ---- SCENE FILTER  --- BEGIN
set(PROJECT_SCENE_SRC_DIR ${PROJECT_SRC_DIR}scene/)
set(SCENE_FILTER
    ${SCENE_FILTER}
    ${PROJECT_SCENE_SRC_DIR}ViewFrustumCulling.cpp
    ${PROJECT_SCENE_SRC_DIR}ViewFrustumCulling.ixx
    ${PROJECT_SCENE_SRC_DIR}Vertex.ixx
    ${PROJECT_SCENE_SRC_DIR}Scene.cpp
    ${PROJECT_SCENE_SRC_DIR}Scene.ixx
    ${PROJECT_SCENE_SRC_DIR}Rotation.ixx
    ${PROJECT_SCENE_SRC_DIR}Quad.cpp
    ${PROJECT_SCENE_SRC_DIR}Quad.ixx
    ${PROJECT_SCENE_SRC_DIR}ObjMaterial.cpp
    ${PROJECT_SCENE_SRC_DIR}ObjMaterial.ixx
    ${PROJECT_SCENE_SRC_DIR}ObjLoader.cpp
    ${PROJECT_SCENE_SRC_DIR}ObjLoader.ixx
    ${PROJECT_SCENE_SRC_DIR}Model.cpp
    ${PROJECT_SCENE_SRC_DIR}Model.ixx
    ${PROJECT_SCENE_SRC_DIR}Mesh.cpp
    ${PROJECT_SCENE_SRC_DIR}Mesh.ixx
    ${PROJECT_SCENE_SRC_DIR}GameObject.cpp
    ${PROJECT_SCENE_SRC_DIR}GameObject.ixx
    ${PROJECT_SCENE_SRC_DIR}AABB.cpp
    ${PROJECT_SCENE_SRC_DIR}AABB.ixx)
# ---- SCENE FILTER  --- END

# ---- WINDOW FILTER  --- BEGIN
set(PROJECT_WINDOW_SRC_DIR ${PROJECT_SRC_DIR}window/)
set(WINDOW_FILTER ${WINDOW_FILTER} ${PROJECT_WINDOW_SRC_DIR}Window.cpp ${PROJECT_WINDOW_SRC_DIR}Window.ixx)
# ---- WINDOW FILTER  --- END

# ---- DEBUG FILTER  --- BEGIN
set(PROJECT_DEBUG_SRC_DIR ${PROJECT_SRC_DIR}debug/)
set(DEBUG_FILTER ${DEBUG_FILTER} ${PROJECT_DEBUG_SRC_DIR}DebugApp.cpp ${PROJECT_DEBUG_SRC_DIR}DebugApp.hpp)
# ---- DEBUG FILTER  --- END

# ---- UTIL FILTER  --- BEGIN
set(PROJECT_UTIL_SRC_DIR ${PROJECT_SRC_DIR}util/)
set(UTIL_FILTER
    ${UTIL_FILTER}
    ${PROJECT_UTIL_SRC_DIR}File.ixx
    ${PROJECT_UTIL_SRC_DIR}File.module.cpp
    ${PROJECT_UTIL_SRC_DIR}RandomNumbers.ixx
    ${PROJECT_UTIL_SRC_DIR}RandomNumbers.module.cpp)
# ---- UTIL FILTER  --- END

# ---- APP FILTER  --- BEGIN
set(PROJECT_APP_SRC_DIR ${PROJECT_SRC_DIR}app/)
set(APP_FILTER ${APP_FILTER} ${PROJECT_APP_SRC_DIR}App.cpp)
# ---- APP FILTER  --- END

# ---- COMMON FILTER  --- BEGIN
set(PROJECT_COMMON_SRC_DIR ${PROJECT_SRC_DIR})
set(COMMON_FILTER ${COMMON_FILTER})
# ---- COMMON FILTER  --- END

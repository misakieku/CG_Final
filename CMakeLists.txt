cmake_minimum_required(VERSION 3.24)

# Allow older FetchContent subprojects (e.g., GLFW 3.4) under CMake 4.x
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

project(CG_Final VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link MSVC runtime statically so executables run standalone on any Windows machine
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Place executables in build/bin/<Config>/ (and build/bin/ for single-config)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(OUTPUTCONFIG Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${OUTPUTCONFIG}" OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}")
endforeach()

include(FetchContent)

# -----------------------------------------------------------------------------
# 1. GLFW
# -----------------------------------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# -----------------------------------------------------------------------------
# 2. GLAD (OpenGL 4.6 Core Profile)
# -----------------------------------------------------------------------------
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.8
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake
)

# -----------------------------------------------------------------------------
# 3. GLM
# -----------------------------------------------------------------------------
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

# -----------------------------------------------------------------------------
# 4. Dear ImGui (docking branch)
# -----------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw glad glm imgui)

glad_add_library(glad STATIC API gl:core=4.6)

# Build Dear ImGui as a static library with GLFW + OpenGL3 backends
add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PUBLIC glfw)

# Compiler warnings
function(cg_set_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /utf-8)
        target_compile_definitions(${target_name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Shared internal library (common utilities: GL RAII, .dat I/O, etc.)
# -----------------------------------------------------------------------------
add_library(cg_common STATIC
    src/common/gl_raii.cpp
    src/common/dat_mesh.cpp
)
target_include_directories(cg_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cg_common PUBLIC glfw glad glm::glm)
cg_set_warnings(cg_common)

# -----------------------------------------------------------------------------
# Executable 1: SOR_Modeller.exe
# -----------------------------------------------------------------------------
add_executable(SOR_Modeller
    src/modeller/main.cpp
    src/modeller/profile_curve.cpp
    src/modeller/sor_mesh.cpp
)
target_link_libraries(SOR_Modeller PRIVATE cg_common imgui)
cg_set_warnings(SOR_Modeller)

# -----------------------------------------------------------------------------
# Executable 2: MazeGame.exe
# -----------------------------------------------------------------------------
add_executable(MazeGame
    src/game/main.cpp
    src/game/GameSession.cpp
    src/game/Window.cpp
    src/game/MazeGenerator.cpp
    src/game/MazeSceneBuilder.cpp
    src/game/FoliageGenerator.cpp
    src/game/OasisGenerator.cpp
    src/game/TextureGenerator.cpp
    src/game/VineGenerator.cpp
    src/game/render/GpuBuffer.cpp
    src/game/render/GrassField.cpp
    src/game/render/MegaScene.cpp
    src/game/render/RenderResources.cpp
    src/game/render/Renderer.cpp
    src/game/render/passes/CullingPass.cpp
    src/game/render/passes/DepthPrePass.cpp
    src/game/render/passes/GBufferPass.cpp
    src/game/render/passes/GIPass.cpp
    src/game/render/passes/LightCullingPass.cpp
    src/game/render/passes/CompositePass.cpp
    src/game/render/passes/PuddlePass.cpp
    src/game/render/passes/SSAOPass.cpp
    src/game/render/passes/SSPRPass.cpp
    src/game/render/passes/ShadowPass.cpp
    src/game/render/passes/VolumetricFogPass.cpp
    src/game/render/passes/WaterPass.cpp
    src/game/render/passes/PostProcessPass.cpp
    src/game/render/passes/FxaaPass.cpp
)
target_link_libraries(MazeGame PRIVATE cg_common imgui)
cg_set_warnings(MazeGame)

# Sync external GLSL shaders next to MazeGame.exe on every build for portable Bin/ execution
add_custom_target(MazeShaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:MazeGame>/shaders"
    COMMENT "Syncing GLSL shaders to MazeGame output directory"
)
add_dependencies(MazeGame MazeShaders)

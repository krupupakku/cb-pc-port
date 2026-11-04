cmake_minimum_required(VERSION 3.20)
project(CrashBashRemaster VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    add_compile_options(/W3 /MP)
else()
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    add_compile_options(-Wall -Wextra -Wno-ignored-qualifiers -Wno-delete-non-abstract-non-virtual-dtor -Wno-missing-field-initializers)
    # Link statico della runtime C++ per poter avviare gli .exe con doppio click da Esplora File
    add_link_options(-static)
endif()

# =============================================================================
# 1. Tool: cb_extract (Estrattore CRASHBSH.DAT migliorato a 64-bit)
# =============================================================================
add_executable(cb_extract
    tools/cb_extract/main.cpp
)

# =============================================================================
# 2. Tool: cb_model2obj (Convertitore .mdl + .tex -> .obj + .png + animazioni)
#    Compila direttamente i sorgenti di mateusfavarin/crashbash/tools/model
# =============================================================================
set(ORIG_MODEL_TOOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../original/repos/crashbash/tools/model")
if(EXISTS "${ORIG_MODEL_TOOL_DIR}/main.cpp")
    add_executable(cb_model2obj
        "${ORIG_MODEL_TOOL_DIR}/main.cpp"
        "${ORIG_MODEL_TOOL_DIR}/mdl.cpp"
        "${ORIG_MODEL_TOOL_DIR}/mesh.cpp"
        "${ORIG_MODEL_TOOL_DIR}/anim.cpp"
        "${ORIG_MODEL_TOOL_DIR}/object.cpp"
        "${ORIG_MODEL_TOOL_DIR}/tex.cpp"
        "${ORIG_MODEL_TOOL_DIR}/image.cpp"
        "${ORIG_MODEL_TOOL_DIR}/primitives.cpp"
        "${ORIG_MODEL_TOOL_DIR}/filemanager.cpp"
        "${ORIG_MODEL_TOOL_DIR}/filecomponent.cpp"
    )
    target_include_directories(cb_model2obj PRIVATE "${ORIG_MODEL_TOOL_DIR}")
endif()

# =============================================================================
# 3. Engine Principale: crashbash_remaster (PC Native Port & Remaster+)
# =============================================================================
add_executable(crashbash_remaster
    src/main.cpp
    src/core/psyq_shim.cpp
    src/renderer/renderer.cpp
    src/input/input_manager.cpp
    src/audio/audio_manager.cpp
    src/netcode/rollback_manager.cpp
)

target_include_directories(crashbash_remaster PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../original/repos/crashbash"
    "${ORIG_MODEL_TOOL_DIR}/third_party"
)

if(WIN32)
    # Link nativo a XInput, OpenGL e GDI32 per finestra grafica + 4 controller
    target_link_libraries(crashbash_remaster PRIVATE xinput opengl32 gdi32 user32)
endif()

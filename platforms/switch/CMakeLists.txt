cmake_minimum_required(VERSION 3.20)
project(VirtuaRacingDeluxe VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED DEVKITPRO)
    set(DEVKITPRO /opt/devkitpro)
endif()

set(CHAOTIX_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../chaotix/src")

# Core emulation engine sources
set(VR_CPU_SOURCES
    ${CHAOTIX_SRC_DIR}/cpu/m68k/m68k_decode.cpp
    ${CHAOTIX_SRC_DIR}/cpu/m68k/m68k_disasm.cpp
    ${CHAOTIX_SRC_DIR}/cpu/m68k/m68k_interp.cpp
    ${CHAOTIX_SRC_DIR}/cpu/sh2/sh2_decode.cpp
    ${CHAOTIX_SRC_DIR}/cpu/sh2/sh2_interp.cpp
    ${CHAOTIX_SRC_DIR}/cpu/z80/z80.cpp
)

set(VR_RUNTIME_SOURCES
    ${CHAOTIX_SRC_DIR}/runtime/log.cpp
    ${CHAOTIX_SRC_DIR}/runtime/rom.cpp
    ${CHAOTIX_SRC_DIR}/runtime/system.cpp
    ${CHAOTIX_SRC_DIR}/runtime/patches.cpp
    ${CHAOTIX_SRC_DIR}/runtime/stage_select.cpp
    ${CHAOTIX_SRC_DIR}/runtime/md_bus.cpp
    ${CHAOTIX_SRC_DIR}/runtime/sh2_bus.cpp
    ${CHAOTIX_SRC_DIR}/runtime/sh2_onchip.cpp
    ${CHAOTIX_SRC_DIR}/runtime/vdp.cpp
    ${CHAOTIX_SRC_DIR}/renderer/compositor.cpp
    ${CHAOTIX_SRC_DIR}/runtime/save.cpp
    ${CHAOTIX_SRC_DIR}/audio/ym2612.cpp
    ${CHAOTIX_SRC_DIR}/audio/sn76489.cpp
    ${CHAOTIX_SRC_DIR}/renderer/image_io.cpp
    ${CHAOTIX_SRC_DIR}/renderer/viewport.cpp
    ${CHAOTIX_SRC_DIR}/input/input.cpp
)

add_definitions(-D__SWITCH__=1 -DCHAOTIX_USE_SDL2=1 -DNDEBUG=1)

add_executable(VirtuaRacingDeluxe
    main.cpp
    ${VR_CPU_SOURCES}
    ${VR_RUNTIME_SOURCES}
)

target_include_directories(VirtuaRacingDeluxe PRIVATE
    ${CHAOTIX_SRC_DIR}
    ${DEVKITPRO}/portlibs/switch/include
    ${DEVKITPRO}/libnx/include
)

target_compile_options(VirtuaRacingDeluxe PRIVATE
    -march=armv8-a+crc+crypto
    -mtune=cortex-a57
    -mtp=soft
    -fPIE
    -fexceptions
    -O3
    -flto
    -fomit-frame-pointer
    -finline-functions
    -fno-stack-protector
    -fno-rtti
    -Wall
    -Wextra
    -Wno-unused-parameter
)

target_link_options(VirtuaRacingDeluxe PRIVATE
    -flto
    -O3
)

target_link_libraries(VirtuaRacingDeluxe PRIVATE
    -Wl,--allow-multiple-definition
    -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau
    -lfreetype -lharfbuzz -lbz2 -lpng -lz -lnx -lm
)

# RomFS Directory Setup
set(VR_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${VR_ROMFS_DIR}")

# Copy assembled ROM to RomFS
set(ROM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../build/vr_rebuild.32x")
if(EXISTS "${ROM_SRC}")
    file(COPY_FILE "${ROM_SRC}" "${VR_ROMFS_DIR}/rom.bin" ONLY_IF_DIFFERENT)
endif()

# NACP Metadata
nx_generate_nacp(virtuaracing_nacp
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/VirtuaRacingDeluxe.nacp"
    NAME "Virtua Racing Deluxe"
    AUTHOR "Thorhax"
    VERSION "${PROJECT_VERSION}"
)

# NRO Target
nx_create_nro(virtuaracing_nro
    TARGET VirtuaRacingDeluxe
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/VirtuaRacingDeluxe.nro"
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"
    NACP "${CMAKE_CURRENT_BINARY_DIR}/VirtuaRacingDeluxe.nacp"
    ROMFS "${VR_ROMFS_DIR}"
)

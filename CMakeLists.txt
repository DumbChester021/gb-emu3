cmake_minimum_required(VERSION 3.16)
project(gb-emu3 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Source files
set(SOURCES
    src/main.cpp
    src/Emulator.cpp
    
    # CPU
    src/cpu/CPU.cpp
    src/cpu/Instructions.cpp
    src/cpu/InstructionsCB.cpp
    src/cpu/InterruptController.cpp
    
    # PPU
    src/ppu/PPU.cpp
    
    # APU
    src/apu/APU.cpp
    
    # Timer
    src/timer/Timer.cpp
    
    # Input
    src/input/Joypad.cpp
    
    # Serial
    src/serial/Serial.cpp
    
    # Memory
    src/memory/Bus.cpp
    src/memory/Memory.cpp
    src/memory/DMA.cpp
    src/memory/BootROM.cpp
    
    # Cartridge
    src/cartridge/Cartridge.cpp
    
    # Frontend
    src/frontend/Window.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES})

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE
    ${SDL2_CFLAGS_OTHER}
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
)

# Release optimizations
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native>
)

message(STATUS "Configured ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "SDL2 Libraries: ${SDL2_LIBRARIES}")

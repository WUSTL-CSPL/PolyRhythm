cmake_minimum_required(VERSION 3.5)

project (PolyRhythm)


# CMake flags
set(CMAKE_CXX_FLAGS -pthread)
set(CMAKE_C_FLAGS -pthread)

# Change this flag if we test on Arm platforms
#add_compile_definitions(x86)

# add_compile_definitions(RL_ONLINE)


FILE(GLOB srcs src/*.c)

# Add an executable 
add_executable(polyrhythm PolyRhythm.c ${srcs})
# add_executable(polyrhythm PolyRhythm.c)
add_executable(rl PolyRhythm_RL.c ${srcs})
target_compile_definitions(rl PUBLIC -DRL_ONLINE -DTIMER)

add_executable(launcher rt-launcher.c)

target_include_directories(polyrhythm PRIVATE 
        ${PROJECT_SOURCE_DIR}/include
)

target_include_directories(rl PRIVATE 
        ${PROJECT_SOURCE_DIR}/include
)

cmake_minimum_required(VERSION 3.10)
project(IOGL-Homework)

# C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

# --------------------------------------------------------------------------
# Common library (shared sources used by all Lighting_* executables)
# --------------------------------------------------------------------------
add_library(common STATIC
    Camera.cpp
    ShaderProgram.cpp
    Mesh.cpp
    Texture2D.cpp
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENGL_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIRS}
)

target_link_libraries(common PUBLIC
    OpenGL::OpenGL
    GLEW::GLEW
    glfw
)
# --------------------------------------------------------------------------
# Rendu final (tiny_gltf + json + stb)
# --------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Rendu_Final.cpp")
    add_executable(Rendu_Final Rendu_Final.cpp)
    target_link_libraries(Rendu_Final PRIVATE common)
endif()

# --------------------------------------------------------------------------
# Symlink asset directories into build dir so executables find them
# --------------------------------------------------------------------------
foreach(ASSET_DIR shaders models textures)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ASSET_DIR}")
        file(CREATE_LINK
            "${CMAKE_CURRENT_SOURCE_DIR}/${ASSET_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/${ASSET_DIR}"
            SYMBOLIC
        )
    endif()
endforeach()

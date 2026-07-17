cmake_minimum_required(VERSION 3.10)
project(audio_engine)

set(CMAKE_CXX_STANDARD 17)

# Ruta a las librerías precompiladas
set(LIBS_DIR "${CMAKE_SOURCE_DIR}/libs")

# libFLAC estática
add_library(flac STATIC IMPORTED)
set_target_properties(flac PROPERTIES
    IMPORTED_LOCATION "${LIBS_DIR}/${ANDROID_ABI}/libFLAC.a"
)

# libogg estática (dependencia de FLAC)
add_library(ogg STATIC IMPORTED)
set_target_properties(ogg PROPERTIES
    IMPORTED_LOCATION "${LIBS_DIR}/${ANDROID_ABI}/libogg.a"
)

# Todos los fuentes
add_library(audio_engine SHARED
    audio_engine.cpp
    aaudio_utils.cpp
    ring_buffer.cpp
    wav_handler.cpp
    flac_handler.cpp
    media_handler.cpp
    engine_state.cpp
    engine_threads.cpp
    dispatcher.cpp
)

target_compile_definitions(audio_engine PRIVATE __ANDROID_API__=29)

target_include_directories(audio_engine PRIVATE
    "${LIBS_DIR}/include"
)

find_library(aaudio-lib aaudio)
find_library(mediandk-lib mediandk)

target_link_libraries(audio_engine
    flac
    ogg
    ${aaudio-lib}
    ${mediandk-lib}
    log
)

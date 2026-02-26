cmake_minimum_required(VERSION 3.22)

# On macOS, point CMake at the system compilers (Xcode or Command Line Tools)
if(APPLE)
    set(CLANG_PATH "")
    if(EXISTS "/usr/bin/clang")
        set(CLANG_PATH "/usr/bin")
    elseif(EXISTS "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang")
        set(CLANG_PATH "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin")
    endif()
    if(CLANG_PATH)
        set(CMAKE_C_COMPILER "${CLANG_PATH}/clang")
        set(CMAKE_CXX_COMPILER "${CLANG_PATH}/clang++")
    endif()
endif()

project(Ping VERSION 1.0.0)

# Download JUCE automatically (no separate install needed)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE
    GIT_TAG        b77249ad52c96ef7293d5ef9518aabb793c5ac92
)
FetchContent_MakeAvailable(JUCE)

# Install plugins to main /Library (not user ~/Library)
if(APPLE)
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY JUCE_AU_COPY_DIR   "/Library/Audio/Plug-Ins/Components")
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY JUCE_VST3_COPY_DIR  "/Library/Audio/Plug-Ins/VST3")
endif()

# libsodium for licence verification
find_path(SODIUM_INCLUDE_DIR sodium.h
    HINTS /opt/homebrew/opt/libsodium/include /usr/local/include)
find_library(SODIUM_LIBRARY sodium
    HINTS /opt/homebrew/opt/libsodium/lib /usr/local/lib)
if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR "libsodium not found. Install with: brew install libsodium")
endif()

# P!NG AU / VST3 plugin
juce_add_plugin(Ping
    COMPANY_NAME          "Ping"
    PLUGIN_MANUFACTURER_CODE Ping
    PLUGIN_CODE           Png1
    FORMATS               AU VST3
    AUVALIDATE            TRUE
    IS_SYNTH              FALSE
    NEEDS_MIDI_INPUT      FALSE
    NEEDS_MIDI_OUTPUT     FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # Install via Installer/output/*.pkg - run: cmake --build build --target installer
    PRODUCT_NAME          "P!NG"
)

juce_generate_juce_header(Ping)

juce_add_binary_data(PingData
    HEADER_NAME PingBinaryData.h
    SOURCES
        Resources/ping_logo.png
        Resources/spitfire_logo.png
        Resources/drywet_legend.png
        Resources/reverse_button.png
)

target_sources(Ping
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/WaveformComponent.cpp
        Source/IRManager.cpp
        Source/PingLookAndFeel.cpp
        Source/EQGraphComponent.cpp
        Source/PresetManager.cpp
)

target_compile_definitions(Ping
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_include_directories(Ping PRIVATE ${SODIUM_INCLUDE_DIR})

target_link_libraries(Ping
    PRIVATE
        PingData
        ${SODIUM_LIBRARY}
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Mac installer target - creates .pkg that prompts for admin and installs to /Library
if(APPLE)
    add_custom_target(installer
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/Installer/build_installer.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS Ping_AU Ping_VST3
        COMMENT "Building P!NG installer package"
    )
endif()

cmake_minimum_required(VERSION 3.22)

# The build sets MELVILLE_VERSION (1.0.<build number>); MELVILLE compares it with the latest release to offer updates.
set(MELVILLE_VERSION "1.0.0" CACHE STRING "Version of this build")
project(MELVILLE VERSION ${MELVILLE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MELVILLE_BUILD_APP   "Build MELVILLE with every part in it" ON)
option(MELVILLE_BUILD_TESTS "Build the headless core tests" OFF)

# ---------------------------------------------------------------------------
# The parts: checkouts of each app's repo, next to this one by default
# (the workflow checks them out into parts/).
# ---------------------------------------------------------------------------
set(MELVILLE_PARTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/parts" CACHE PATH "Where the app checkouts are")
set(HAWTHORNE_DIR   "${MELVILLE_PARTS_DIR}/hawthorne"   CACHE PATH "ShallowMouse/hawthorne")
set(EMERSON_DIR     "${MELVILLE_PARTS_DIR}/emerson"     CACHE PATH "ShallowMouse/emerson")
set(WHITMAN_DIR     "${MELVILLE_PARTS_DIR}/whitman"     CACHE PATH "ShallowMouse/whitman")
set(THOREAU_DIR     "${MELVILLE_PARTS_DIR}/thoreau"     CACHE PATH "ShallowMouse/thoreau")
set(PEQUOD_HOST_DIR "${MELVILLE_PARTS_DIR}/pequod-host" CACHE PATH "ShallowMouse/PEQUOD-HOST")

foreach(_part HAWTHORNE EMERSON WHITMAN THOREAU PEQUOD_HOST)
    if(NOT EXISTS "${${_part}_DIR}/Source")
        message(FATAL_ERROR "No ${_part} checkout at ${${_part}_DIR} (set ${_part}_DIR or MELVILLE_PARTS_DIR)")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# JUCE: the same version the apps use
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(CORE_SOURCES
    Source/Core/Patchbay.cpp
    Source/Core/Hub.cpp
    Source/Core/PartDevice.cpp
    Source/Core/Bridges.cpp
    Source/Core/NetworkNode.cpp
    Source/Hosted/MelvilleHosted.cpp)

# the link headers MELVILLE's bridges share with the apps
set(LINK_INCLUDES
    "${HAWTHORNE_DIR}/Source/Route"
    "${HAWTHORNE_DIR}/Source/Network"
    "${EMERSON_DIR}/Source/Link")

set(COMMON_DEFS
    MELVILLE_HOSTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MELVILLE_BUILD_APP)
    include(cmake/Parts.cmake)
endif()

# ---------------------------------------------------------------------------
# Headless tests: the hub, part devices, patchbay and both bridges, with fake parts
# ---------------------------------------------------------------------------
if(MELVILLE_BUILD_TESTS)
    juce_add_console_app(MelvilleTests PRODUCT_NAME "MelvilleTests")
    target_sources(MelvilleTests PRIVATE ${CORE_SOURCES} Tests/CoreTests.cpp
        "${HAWTHORNE_DIR}/Source/Network/NetReceiver.cpp"
        "${HAWTHORNE_DIR}/Source/Network/NetSender.cpp"
        "${HAWTHORNE_DIR}/Source/Engine/Varispeed.cpp")
    target_include_directories(MelvilleTests PRIVATE ${LINK_INCLUDES} Source/Hosted)
    target_compile_definitions(MelvilleTests PRIVATE ${COMMON_DEFS}
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_ASIO=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MelvilleTests PRIVATE
        juce::juce_audio_devices
        juce::juce_events
        juce::juce_data_structures
        juce::juce_recommended_config_flags)
endif()

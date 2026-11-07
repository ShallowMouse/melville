# MELVILLE with every part in it: one program, built from each app's own sources.
#
# Each app's files are compiled with that app's own settings (its name and version
# strings, its include folder), set per file, so the apps' code goes in unchanged.
# A few names several apps define the same way - their copies of the PEQUOD look
# (namespace Pequod, PowerButton, PeakMeter, ...) and the Hawthorne Link sender THOREAU
# and PEQUOD Host carry - are renamed per app with macros here, so the copies don't
# collide in one program.

# ---------------------------------------------------------------------------
# The PEQUOD engine (Rust), as PEQUOD Host and EMERSON link it
# ---------------------------------------------------------------------------
if(WIN32)
    set(_pq_default "${PEQUOD_HOST_DIR}/engine/target/x86_64-pc-windows-msvc/release/pequod_engine.lib")
else()
    set(_pq_default "${PEQUOD_HOST_DIR}/engine/target/release/libpequod_engine.a")
endif()
set(PEQUOD_ENGINE_LIB "${_pq_default}" CACHE FILEPATH "The built PEQUOD engine library")
if(NOT EXISTS "${PEQUOD_ENGINE_LIB}")
    message(FATAL_ERROR "PEQUOD engine not built: see ${PEQUOD_HOST_DIR}/engine/README.md (cargo build --release -p pequod_engine)")
endif()
set(PEQUOD_ENGINE_NATIVE_LIBS)
if(EXISTS "${PEQUOD_ENGINE_LIB}.libs")
    file(READ "${PEQUOD_ENGINE_LIB}.libs" _pq_libs)
    string(REGEX MATCHALL "[^ \t\r\n]+" _pq_tokens "${_pq_libs}")
    foreach(_t IN LISTS _pq_tokens)
        if(_t MATCHES "^(.+)\\.lib$")
            list(APPEND PEQUOD_ENGINE_NATIVE_LIBS "${CMAKE_MATCH_1}")
        elseif(_t MATCHES "^-l(.+)$")
            list(APPEND PEQUOD_ENGINE_NATIVE_LIBS "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES PEQUOD_ENGINE_NATIVE_LIBS)
    list(REMOVE_ITEM PEQUOD_ENGINE_NATIVE_LIBS c msvcrt libcmt ucrt libucrt vcruntime libvcruntime)
elseif(WIN32)
    set(PEQUOD_ENGINE_NATIVE_LIBS kernel32 advapi32 bcrypt ntdll userenv ws2_32 ole32 oleaut32 shell32 user32 gdi32 opengl32 dwmapi uxtheme crypt32 secur32 propsys uuid)
else()
    set(PEQUOD_ENGINE_NATIVE_LIBS xcb GL X11-xcb X11 pthread dl m rt util)
endif()
get_filename_component(PEQUOD_ENGINE_DIR "${PEQUOD_ENGINE_LIB}" DIRECTORY)

# ---------------------------------------------------------------------------
# One set of resources for everyone: the PEQUOD fonts (the same files in every app)
# and Hawthorne's mark and manual. The apps all use BinaryData::...
# ---------------------------------------------------------------------------
juce_add_binary_data(MelvilleBinaryData HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES
    "${PEQUOD_HOST_DIR}/Resources/Fonts/CourierPrime-Regular.ttf"
    "${PEQUOD_HOST_DIR}/Resources/Fonts/CourierPrime-Bold.ttf"
    "${PEQUOD_HOST_DIR}/Resources/Fonts/Roboto-Regular.ttf"
    "${PEQUOD_HOST_DIR}/Resources/Fonts/Roboto-Bold.ttf"
    "${PEQUOD_HOST_DIR}/Resources/Fonts/Ubuntu-Light.ttf"
    "${PEQUOD_HOST_DIR}/Resources/Fonts/Hack-Regular.ttf"
    "${HAWTHORNE_DIR}/Resources/hawthorne-mark.svg"
    "${HAWTHORNE_DIR}/Resources/manual.html"
    # each part's own icon, for the MELVILLE bar
    "${HAWTHORNE_DIR}/Resources/hawthorne-logo.png"
    "${EMERSON_DIR}/Resources/Icon/EMERSON.png"
    "${WHITMAN_DIR}/Resources/Icon/WHITMAN.png"
    "${THOREAU_DIR}/Resources/Icon/THOREAU.png"
    "${PEQUOD_HOST_DIR}/Resources/Icon/PEQUOD-Host.png")

set(_icon "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon/MELVILLE.png")
if(EXISTS "${_icon}")
    set(_icon_args ICON_BIG "${_icon}" ICON_SMALL "${_icon}")
endif()

juce_add_gui_app(Melville
    PRODUCT_NAME "MELVILLE"
    COMPANY_NAME "PEQUOD"
    BUNDLE_ID    "com.pequod.melville"
    ${_icon_args}
    VERSION      ${PROJECT_VERSION})
juce_generate_juce_header(Melville)

# ---------------------------------------------------------------------------
# MELVILLE's own files
# ---------------------------------------------------------------------------
set(MELVILLE_OWN_SOURCES
    ${CORE_SOURCES}
    Source/Main.cpp
    Source/UI/PatchbayView.cpp
    Source/UI/Taskbar.cpp
    Source/Updater.cpp)
target_sources(Melville PRIVATE ${MELVILLE_OWN_SOURCES})
set_source_files_properties(${MELVILLE_OWN_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "MELVILLE_VERSION_STRING=\"${PROJECT_VERSION}\""
    INCLUDE_DIRECTORIES "${LINK_INCLUDES}")

# ---------------------------------------------------------------------------
# A part: its files, with its own name, version and include folders, and renames
# ---------------------------------------------------------------------------
set(PART_VERSION "${PROJECT_VERSION}")   # the parts show MELVILLE's build

function(melville_part)
    cmake_parse_arguments(P "" "NAME;DIR;MAKER" "SOURCES;DEFS;INCLUDES" ${ARGN})
    set(_files)
    foreach(_s IN LISTS P_SOURCES)
        list(APPEND _files "${P_DIR}/${_s}")
    endforeach()
    list(APPEND _files "${CMAKE_CURRENT_SOURCE_DIR}/Source/Parts/${P_MAKER}")
    target_sources(Melville PRIVATE ${_files})
    set_source_files_properties(${_files} PROPERTIES
        COMPILE_DEFINITIONS "JUCE_APPLICATION_NAME_STRING=\"${P_NAME}\";JUCE_APPLICATION_VERSION_STRING=\"${PART_VERSION}\";${P_DEFS}"
        INCLUDE_DIRECTORIES "${P_DIR}/Source;${CMAKE_CURRENT_SOURCE_DIR}/Source/Parts;${P_INCLUDES}")
endfunction()

# the PEQUOD look's widgets and the updater, renamed per app that carries a copy of them
function(pequod_look_renames OUT PREFIX)
    set(${OUT}
        "Pequod=Pequod${PREFIX}"
        "PowerButton=${PREFIX}PowerButton"
        "PeakMeter=${PREFIX}PeakMeter"
        "LearnableSlider=${PREFIX}LearnableSlider"
        "LearnableButton=${PREFIX}LearnableButton"
        "Updater=${PREFIX}Updater"
        PARENT_SCOPE)
endfunction()

# Hawthorne: the tape machine (owns Hawthorne Route's memory - MELVILLE's copy of it)
file(STRINGS "${HAWTHORNE_DIR}/CMakeLists.txt" _hv REGEX "project\\(Hawthorne VERSION")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" HAWTHORNE_VERSION "${_hv}")
melville_part(NAME "Hawthorne" DIR "${HAWTHORNE_DIR}" MAKER MakeHawthorne.cpp
    SOURCES
        Source/Engine/Reel.cpp Source/Engine/Session.cpp Source/Engine/Library.cpp
        Source/Engine/RecordingStream.cpp Source/Engine/Varispeed.cpp Source/Engine/RouteHost.cpp
        Source/Engine/Mtc.cpp Source/Network/NetReceiver.cpp Source/Network/NetSender.cpp
        Source/Engine/TapeEngine.cpp
        Source/Updater.cpp
        Source/UI/Logo.cpp Source/UI/ChannelModule.cpp Source/UI/MarkerPanel.cpp
        Source/UI/SessionSheet.cpp Source/UI/LibraryView.cpp Source/UI/VUMeter.cpp
        Source/UI/RoutingMatrix.cpp Source/UI/NetworkWindow.cpp Source/UI/SyncWindow.cpp
        Source/UI/MainComponent.cpp Source/UI/Layout.cpp Source/UI/Blocks.cpp
    DEFS "HAWTHORNE_VERSION=\"${HAWTHORNE_VERSION}\"")

# EMERSON: the FX rack (its PEQUOD processors come from PEQUOD Host's engine, built once below)
pequod_look_renames(_emerson_renames Emerson)
melville_part(NAME "EMERSON" DIR "${EMERSON_DIR}" MAKER MakeEmerson.cpp
    SOURCES
        Source/Engine/Rack.cpp Source/Engine/RackState.cpp Source/Link/LinkHost.cpp
        Source/MainComponent.cpp Source/RackPanel.cpp Source/TrackList.cpp
        Source/Plugins.cpp Source/RouteStatus.cpp Source/Updater.cpp
    DEFS "${_emerson_renames}"
    INCLUDES "${PEQUOD_HOST_DIR}/Source")

# WHITMAN: the desks
pequod_look_renames(_whitman_renames Whitman)
melville_part(NAME "WHITMAN" DIR "${WHITMAN_DIR}" MAKER MakeWhitman.cpp
    SOURCES
        Source/MixEngine.cpp Source/BandEq.cpp Source/CircuitSim.cpp Source/MixState.cpp
        Source/RouteStatus.cpp Source/MainComponent.cpp Source/Console.cpp
        Source/DeskPanels.cpp Source/Updater.cpp
    DEFS "${_whitman_renames}")

# THOREAU: the drum machine (its Hawthorne Link sender is its own copy: renamed)
pequod_look_renames(_thoreau_renames Thoreau)
melville_part(NAME "THOREAU" DIR "${THOREAU_DIR}" MAKER MakeThoreau.cpp
    SOURCES
        Source/DrumVoices.cpp Source/AcousticDrum.cpp Source/Kits.cpp Source/KitBus.cpp
        Source/DrumEngine.cpp Source/Project.cpp Source/TapeChase.cpp
        Source/HawthorneLink/NetSender.cpp
        Source/MainComponent.cpp Source/StepGrid.cpp Source/VoicePanel.cpp Source/SongPanel.cpp
        Source/KitPanel.cpp Source/SettingsPanel.cpp Source/Updater.cpp
    DEFS "${_thoreau_renames};hawthorne=thoreau_hawthorne")

# PEQUOD Host: the guitar rig, and the PEQUOD engine's C++ side EMERSON uses too
melville_part(NAME "PEQUOD Host" DIR "${PEQUOD_HOST_DIR}" MAKER MakePequod.cpp
    SOURCES
        Source/HostEngine.cpp Source/ControlSurface.cpp Source/MainComponent.cpp Source/RackView.cpp
        Source/BrowserPanel.cpp Source/HeaderBar.cpp Source/ControlPanel.cpp Source/ChainMap.cpp
        Source/NativeClip.cpp Source/RackProcessor.cpp Source/PresetLibrary.cpp
        Source/SettingsPanel.cpp Source/PluginManagerPanel.cpp Source/ControllerLink.cpp
        Source/ControllerManager.cpp Source/ControllerPanel.cpp Source/Updater.cpp Source/CrashGuard.cpp
        Source/ScreenLayout.cpp Source/ScreenEditor.cpp Source/HawthorneLink/NetSender.cpp
        Source/Modifiers.cpp Source/ModifierRow.cpp Source/SceneBar.cpp
        Source/PequodNative.cpp Source/PequodPanel.cpp Source/WorkerPool.cpp
    DEFS "hawthorne=pequod_hawthorne")

# ---------------------------------------------------------------------------
target_compile_definitions(Melville PRIVATE ${COMMON_DEFS}
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_include_directories(Melville PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Hosted")
target_link_directories(Melville PRIVATE "${PEQUOD_ENGINE_DIR}")
target_link_libraries(Melville
    PRIVATE
        "${PEQUOD_ENGINE_LIB}"
        ${PEQUOD_ENGINE_NATIVE_LIBS}
        MelvilleBinaryData
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Melville PRIVATE /bigobj)
endif()

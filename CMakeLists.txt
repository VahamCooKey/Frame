# Frame — owned by lead. Other agents must not add files to this target;
# request it via CONTRACTS.md.
cmake_minimum_required(VERSION 3.24)

project(Frame VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)

qt_standard_project_setup()

# --- platform seam (A27): one .cpp per OS, selected here — nowhere else in
# the build. src/platform/NativeWindowFactory.cpp holds the matching #ifdef
# that picks the class; main.cpp never names a concrete platform class.
if(WIN32)
    set(FRAME_PLATFORM_SOURCES src/platform/NativeWindowWin32.cpp src/platform/WindowsMediaSession.cpp)
    set(FRAME_PLATFORM_LIBS dwmapi ole32 uuid windowsapp)
elseif(APPLE)
    # UNVERIFIED (notes/cross-platform-plan.md): written against the Qt API,
    # never built — no macOS toolchain on this machine. Plain C++ for now;
    # becomes a .mm file when the real-traffic-lights/reduceMotion work
    # (plan §4.3, §1.3) needs Objective-C++.
    set(FRAME_PLATFORM_SOURCES src/platform/NativeWindowMac.cpp)
    set(FRAME_PLATFORM_LIBS "")
else()
    # UNVERIFIED (notes/cross-platform-plan.md): same caveat, Linux/X11+Wayland.
    find_package(Qt6 6.8 REQUIRED COMPONENTS DBus)
    set(FRAME_PLATFORM_SOURCES src/platform/NativeWindowLinux.cpp
        src/platform/LinuxPlaybackInhibitor.cpp src/platform/LinuxMediaSession.cpp)
    set(FRAME_PLATFORM_LIBS Qt6::DBus)
endif()

set(FRAME_SOURCES
    src/main.cpp
    src/core/PlayerController.cpp src/core/ClipboardCapture.cpp
    src/core/TrackPreference.cpp
    src/core/PlaylistSession.cpp
    src/core/PlaybackPreferences.cpp
    src/core/MpvRenderItem.cpp
    src/core/SettingsStore.cpp
    src/core/ThumbnailGenerator.cpp
    src/core/AudioOverview.cpp
    src/core/MediaNameFormatter.cpp
    src/core/ReleaseChecker.cpp
    src/core/SingleInstance.cpp
    src/core/LogSanitizer.cpp
    src/core/FileLogger.cpp
    src/core/FontWarmup.cpp
    src/core/GuiStallMonitor.cpp
    src/input/ShortcutManager.cpp
    src/input/MenuBuilder.cpp
    src/platform/NativeWindowBase.cpp
    src/platform/NativeWindowFactory.cpp
    src/platform/SystemMediaControls.cpp
    src/platform/MediaSession.cpp
    ${FRAME_PLATFORM_SOURCES}
    # A26: header-only, but it now carries Q_OBJECT, so AUTOMOC has to
    # see it. Listing it here is what guarantees that.
    src/platform/INativeWindow.h
)

qt_add_executable(frame WIN32 ${FRAME_SOURCES})
target_include_directories(frame PRIVATE src src/core src/input src/platform)
# Single version source: project() above feeds main.cpp and the .rc.
target_compile_definitions(frame PRIVATE FRAME_VERSION="${PROJECT_VERSION}")
configure_file(src/frame.rc.in frame.rc @ONLY)
target_sources(frame PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/frame.rc)

# --- libmpv ---
if(WIN32)
    # Prebuilt dev drop.
    add_library(mpv SHARED IMPORTED)
    set_target_properties(mpv PROPERTIES
        IMPORTED_LOCATION            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.dll"
        IMPORTED_IMPLIB              "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/include"
    )
else()
    # UNVERIFIED (plan §3): libmpv is packaged everywhere on Linux
    # (pkg-config `mpv`); Homebrew ships it on macOS too, though a shipped
    # bundle there eventually needs @rpath fixups (plan §3, "S-M") that this
    # does not attempt. Never resolved on this machine — no pkg-config here.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    add_library(mpv ALIAS PkgConfig::MPV)
endif()

target_link_libraries(frame PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network
    mpv ${FRAME_PLATFORM_LIBS}
)

if(WIN32)
    add_executable(frame-associations src/platform/FileAssociationsWin.cpp src/platform/UserChoiceHashWin.cpp)
    target_link_libraries(frame-associations PRIVATE Qt6::Core advapi32 shlwapi shell32 user32)
    add_dependencies(frame frame-associations)
endif()

# --- QML module ---
qt_policy(SET QTP0004 NEW)
set_source_files_properties(qml/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/KeyNames.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Fonts.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(frame
    URI Frame
    VERSION 1.0
    RESOURCE_PREFIX /
    # Explicit, lowercase, and distinct from the `frame` target/binary name:
    # the default output dir is CMAKE_CURRENT_BINARY_DIR/Frame, which
    # collides with the `frame` executable on case-insensitive filesystems
    # (an out-of-source Linux build on /mnt/d hit exactly this under the old
    # GDPlayer/gdplayer names). Nesting under qml-module/ sidesteps it
    # regardless of case-sensitivity.
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml-module"
    QML_FILES
        qml/main.qml
        qml/SubtitleLoadingNotice.qml
        qml/SubtitleSafeArea.qml
        qml/DeferredOnScreenController.qml
        qml/DeferredTitleBar.qml
        qml/DeferredPlaylistDrawer.qml
        qml/DeferredContextMenu.qml
        qml/TrackMenu.qml
        qml/ChapterPicker.qml
        qml/ChapterTicks.qml
        qml/BookmarkTicks.qml
        qml/GoToDialog.qml
        qml/DeferredGoToDialog.qml
        qml/PlaybackInfo.qml
        qml/DeferredPlaybackInfo.qml
        qml/ShortcutGuide.qml
        qml/DeferredShortcutGuide.qml
        qml/ToggleSwitch.qml
        qml/LoadingSpinner.qml
        qml/OpenUrlDialog.qml
        qml/DeferredOpenUrlDialog.qml
        qml/DeferredFileDialog.qml
        qml/SettingsDialog.qml
        qml/DeferredSettingsDialog.qml
        qml/SettingsGeneralPane.qml
        qml/SettingsVideoPane.qml
        qml/SettingsAudioPane.qml
        qml/SettingsSubtitlesPane.qml
        qml/SettingsInterfacePane.qml
        qml/SettingsAdvancedPane.qml
        qml/SettingGroup.qml
        qml/SettingChoice.qml
        qml/PlaylistPosterCache.qml
        qml/MediaDropOverlay.qml
        qml/SettingRow.qml
        qml/SettingToggle.qml
        qml/SettingDropdown.qml
        qml/SettingSlider.qml
        qml/FlatButton.qml
        qml/PlaybackFailure.qml
        qml/IconButton.qml
        qml/TitleBar.qml
        qml/OnScreenController.qml
        qml/ContextMenu.qml
        qml/PlaylistDrawer.qml
        qml/Colors.qml
        qml/KeyNames.qml
        qml/KeyCaps.qml
        qml/ShortcutHints.qml
        qml/ScreenshotToast.qml
        qml/AudioView.qml
        qml/Fonts.qml
)

# --- runtime deployment into the build dir ---
get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)

qt6_add_resources(frame "mpv_conf"
    PREFIX "/"
    FILES assets/input.conf
)
qt6_add_resources(frame "ui_icons"
    PREFIX "/"
    FILES
        assets/icons/play.svg
        assets/icons/play-outline.svg
        assets/icons/pin.svg
        assets/icons/aspect.svg
        assets/icons/loop.svg
        assets/icons/bookmark.svg
        assets/icons/gauge.svg
        assets/icons/folder.svg
        assets/icons/link.svg
        assets/icons/clock.svg
        assets/icons/theater.svg
        assets/icons/window.svg
        assets/icons/picture.svg
        assets/icons/pause.svg
        assets/icons/pause-outline.svg
        assets/icons/seek-back.svg
        assets/icons/seek-forward.svg
        assets/icons/skip-previous.svg
        assets/icons/skip-next.svg
        assets/icons/chevron-down.svg
        assets/icons/search.svg
        assets/icons/more.svg
        assets/icons/chevron-left.svg
        assets/icons/chevron-right.svg
        assets/icons/volume.svg
        assets/icons/volume-muted.svg
        assets/icons/captions.svg
        assets/icons/minus.svg
        assets/icons/plus.svg
        assets/icons/audio.svg
        assets/icons/playlist.svg
        assets/icons/fullscreen.svg
        assets/icons/fullscreen-exit.svg
        assets/icons/minimize.svg
        assets/icons/maximize.svg
        assets/icons/restore.svg
        assets/icons/close.svg
        assets/icons/trash.svg
        assets/icons/check.svg
        assets/icons/loading.svg
        assets/icons/repeat.svg
        assets/icons/shuffle.svg
        # A26: replace the Segoe MDL2 Assets footer glyphs, which are
        # private-use codepoints and tofu without that Windows font.
        assets/icons/settings.svg
        assets/icons/update.svg
        assets/icons/info.svg
        assets/icons/github.svg
        # Settings category navigation (2026-11-05 user decision).
        assets/icons/nav-playback.svg
        assets/icons/nav-video.svg
        assets/icons/nav-audio.svg
        assets/icons/nav-subtitles.svg
        assets/icons/nav-appearance.svg
        assets/icons/nav-shortcuts.svg
        assets/icons/nav-advanced.svg
)
qt6_add_resources(frame "brand_artwork"
    PREFIX "/"
    FILES assets/branding/concepts/frame-player-icon-v1.svg
)
qt6_add_resources(frame "interface_fonts"
    PREFIX "/"
    FILES
        assets/fonts/Inter-Regular.ttf
        assets/fonts/Inter-Medium.ttf
        assets/fonts/Inter-SemiBold.ttf
        assets/fonts/Inter-OFL.txt
)
if(WIN32)
    add_custom_command(TARGET frame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.dll"
            "$<TARGET_FILE_DIR:frame>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/input.conf"
            "$<TARGET_FILE_DIR:frame>"
        COMMAND "${_qt_bin_dir}/windeployqt.exe" --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                --no-translations "$<TARGET_FILE:frame>"
        VERBATIM
    )
else()
    # macdeployqt / linuxdeploy packaging is plan §4.6 — not wired up yet.
    # libmpv comes from the system package on these platforms, so there is
    # nothing to copy for it; input.conf still needs to sit next to the
    # binary for a dev build to run at all.
    add_custom_command(TARGET frame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/input.conf"
            "$<TARGET_FILE_DIR:frame>"
        VERBATIM
    )
endif()

# --- tests (pure logic, no GPU) ---
enable_testing()

# PlayerController delegates clipboard work to ClipboardCapture. The existing
# thumbnail worker remains a separate decoder and scheduling path.
add_executable(test_keymap
    tests/test_keymap.cpp
    src/input/ShortcutManager.cpp
    src/core/PlayerController.cpp src/core/ClipboardCapture.cpp
    src/core/TrackPreference.cpp
    src/core/ThumbnailGenerator.cpp
)
target_include_directories(test_keymap PRIVATE src)
target_link_libraries(test_keymap PRIVATE Qt6::Core Qt6::Gui Qt6::Test mpv)
add_test(NAME test_keymap COMMAND test_keymap)

add_executable(test_menubuilder
    tests/test_menubuilder.cpp
    src/input/MenuBuilder.cpp
)
target_include_directories(test_menubuilder PRIVATE src)
target_link_libraries(test_menubuilder PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_menubuilder COMMAND test_menubuilder)

add_executable(test_audiooverview
    tests/test_audiooverview.cpp
    src/core/AudioOverview.cpp
    src/core/ThumbnailGenerator.cpp
)
target_include_directories(test_audiooverview PRIVATE src)
target_link_libraries(test_audiooverview PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test mpv)
add_test(NAME test_audiooverview COMMAND test_audiooverview)
set_tests_properties(test_audiooverview PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests)

# Silent playlist test: mpv runs headless (vo=libmpv, no render context);
# files are appended with pause=yes so nothing is ever played audibly.
add_executable(test_playlist
    tests/test_playlist.cpp
    src/input/ShortcutManager.cpp
    src/core/PlayerController.cpp src/core/ClipboardCapture.cpp
    src/core/TrackPreference.cpp
    src/core/ThumbnailGenerator.cpp
)
target_include_directories(test_playlist PRIVATE src src/core)
target_link_libraries(test_playlist PRIVATE Qt6::Core Qt6::Gui Qt6::Test mpv)
# WORKING_DIRECTORY makes findAsset() (tests/test_playlist.cpp) resolve
# assets/ relative to the source tree instead of the build tree, so ctest
# passes for out-of-source builds in arbitrary locations (A29: WSL ext4).
add_test(NAME test_playlist COMMAND test_playlist
         -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/test_playlist.xml,junitxml"
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_trackpreference tests/test_trackpreference.cpp src/core/TrackPreference.cpp)
target_include_directories(test_trackpreference PRIVATE src)
target_link_libraries(test_trackpreference PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_trackpreference COMMAND test_trackpreference)

add_executable(test_playlistsession tests/test_playlistsession.cpp
    src/core/PlaylistSession.cpp src/core/SettingsStore.cpp
    src/core/PlayerController.cpp src/core/ClipboardCapture.cpp src/core/TrackPreference.cpp src/core/ThumbnailGenerator.cpp)
target_include_directories(test_playlistsession PRIVATE src)
target_compile_definitions(test_playlistsession PRIVATE FRAME_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_playlistsession PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test mpv)
add_test(NAME test_playlistsession COMMAND test_playlistsession
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/test_playlistsession.xml,junitxml"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_preferences tests/test_preferences.cpp
    src/core/PlaybackPreferences.cpp src/core/SettingsStore.cpp
    src/core/PlayerController.cpp src/core/ClipboardCapture.cpp src/core/TrackPreference.cpp src/core/ThumbnailGenerator.cpp)
target_include_directories(test_preferences PRIVATE src)
target_link_libraries(test_preferences PRIVATE Qt6::Core Qt6::Gui Qt6::Test mpv)
add_test(NAME test_preferences COMMAND test_preferences WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_medianame
    tests/test_medianame.cpp
    src/core/MediaNameFormatter.cpp
)
target_include_directories(test_medianame PRIVATE src)
target_link_libraries(test_medianame PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_medianame COMMAND test_medianame)

add_executable(test_thumbnails tests/test_thumbnails.cpp src/core/ThumbnailGenerator.cpp)
target_include_directories(test_thumbnails PRIVATE src)
target_link_libraries(test_thumbnails PRIVATE Qt6::Core Qt6::Gui Qt6::Test mpv)
add_test(NAME test_thumbnails COMMAND test_thumbnails WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_postercache tests/test_postercache.cpp src/core/ThumbnailGenerator.cpp)
target_include_directories(test_postercache PRIVATE src)
target_compile_definitions(test_postercache PRIVATE FRAME_QML_DIR="${CMAKE_SOURCE_DIR}/qml")
target_link_libraries(test_postercache PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test mpv)
add_test(NAME test_postercache COMMAND test_postercache WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_filedialogs tests/test_filedialogs.cpp)
target_compile_definitions(test_filedialogs PRIVATE FRAME_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_link_libraries(test_filedialogs PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
add_test(NAME test_filedialogs COMMAND test_filedialogs)

add_executable(test_singleinstance tests/test_singleinstance.cpp src/core/SingleInstance.cpp)
target_include_directories(test_singleinstance PRIVATE src)
target_link_libraries(test_singleinstance PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME test_singleinstance COMMAND test_singleinstance
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/test_singleinstance.xml,junitxml")

add_executable(test_logsanitizer tests/test_logsanitizer.cpp src/core/LogSanitizer.cpp)
target_include_directories(test_logsanitizer PRIVATE src)
target_link_libraries(test_logsanitizer PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_logsanitizer COMMAND test_logsanitizer)

add_executable(test_mediasession tests/test_mediasession.cpp src/platform/MediaSession.cpp)
target_include_directories(test_mediasession PRIVATE src)
target_link_libraries(test_mediasession PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_mediasession COMMAND test_mediasession)

if(UNIX AND NOT APPLE)
    find_program(FRAME_DBUS_RUN_SESSION dbus-run-session)
    add_executable(test_linuxdesktop tests/test_linuxdesktop.cpp
        src/platform/MediaSession.cpp src/platform/LinuxMediaSession.cpp
        src/platform/LinuxPlaybackInhibitor.cpp)
    target_include_directories(test_linuxdesktop PRIVATE src)
    target_link_libraries(test_linuxdesktop PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
    if(FRAME_DBUS_RUN_SESSION)
        add_test(NAME test_linuxdesktop COMMAND ${FRAME_DBUS_RUN_SESSION}
            --config-file=${CMAKE_SOURCE_DIR}/tests/fixtures/desktop-session-bus.conf -- $<TARGET_FILE:test_linuxdesktop>)
    else()
        message(WARNING "dbus-run-session missing: Linux mock-bus tests are not registered")
    endif()
endif()

add_executable(test_playerobservations tests/test_playerobservations.cpp
    src/core/PlayerController.cpp src/core/ClipboardCapture.cpp src/core/SettingsStore.cpp
    src/core/TrackPreference.cpp src/core/ThumbnailGenerator.cpp)
target_include_directories(test_playerobservations PRIVATE src)
target_compile_definitions(test_playerobservations PRIVATE FRAME_QML_DIR="${CMAKE_SOURCE_DIR}/qml")
target_link_libraries(test_playerobservations PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test mpv)
add_test(NAME test_playerobservations COMMAND test_playerobservations
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/test_playerobservations.xml,junitxml"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_eventdelivery tests/test_eventdelivery.cpp
    src/core/PlayerController.cpp src/core/ClipboardCapture.cpp
    src/core/TrackPreference.cpp)
target_include_directories(test_eventdelivery PRIVATE src)
target_link_libraries(test_eventdelivery PRIVATE Qt6::Core Qt6::Gui Qt6::Test mpv)
add_test(NAME test_eventdelivery COMMAND test_eventdelivery
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/test_eventdelivery.xml,junitxml"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_filelogger tests/test_filelogger.cpp
    src/core/FileLogger.cpp src/core/LogSanitizer.cpp)
target_include_directories(test_filelogger PRIVATE src)
target_link_libraries(test_filelogger PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_filelogger COMMAND test_filelogger
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/test_filelogger.xml,junitxml")
set_tests_properties(test_filelogger test_eventdelivery PROPERTIES TIMEOUT 45)

get_property(FRAME_TESTS DIRECTORY PROPERTY TESTS)
if(WIN32)
    get_filename_component(FRAME_QT_ROOT "${Qt6Core_DIR}/../../.." ABSOLUTE)
    set_tests_properties(${FRAME_TESTS} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${FRAME_QT_ROOT}/plugins")
else()
    set_tests_properties(${FRAME_TESTS} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Requires an actual OpenGL window (X11, Wayland, or Windows); keep headless
# logic tests usable on CI without a display server.
option(FRAME_ENABLE_RENDER_TESTS "Run OpenGL surface lifecycle tests" OFF)
if(NOT APPLE)
    if(WIN32)
        set(FRAME_MEDIA_TEST_BACKEND src/platform/WindowsMediaSession.cpp)
    else()
        set(FRAME_MEDIA_TEST_BACKEND src/platform/LinuxMediaSession.cpp)
    endif()
    add_executable(test_desktopcontroller tests/test_desktopcontroller.cpp
        src/core/PlayerController.cpp src/core/ClipboardCapture.cpp src/core/TrackPreference.cpp src/core/ThumbnailGenerator.cpp
        src/platform/SystemMediaControls.cpp src/platform/MediaSession.cpp ${FRAME_MEDIA_TEST_BACKEND})
    target_include_directories(test_desktopcontroller PRIVATE src)
    target_compile_definitions(test_desktopcontroller PRIVATE FRAME_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_desktopcontroller PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test mpv ${FRAME_PLATFORM_LIBS})
    if(WIN32 AND FRAME_ENABLE_RENDER_TESTS)
        add_test(NAME test_desktopcontroller COMMAND test_desktopcontroller
            -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/test_desktopcontroller.xml,junitxml")
    elseif(UNIX AND FRAME_DBUS_RUN_SESSION)
        add_test(NAME test_desktopcontroller COMMAND ${FRAME_DBUS_RUN_SESSION}
            --config-file=${CMAKE_SOURCE_DIR}/tests/fixtures/desktop-session-bus.conf -- $<TARGET_FILE:test_desktopcontroller>)
        set_tests_properties(test_desktopcontroller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

if(FRAME_ENABLE_RENDER_TESTS AND NOT APPLE)
    if(WIN32)
        set(FRAME_NATIVE_TEST tests/test_nativedesktop_win.cpp)
    else()
        set(FRAME_NATIVE_TEST tests/test_nativedesktop_linux.cpp)
    endif()
    add_executable(test_nativedesktop ${FRAME_NATIVE_TEST}
        src/platform/MediaSession.cpp src/platform/NativeWindowBase.cpp
        src/platform/INativeWindow.h ${FRAME_PLATFORM_SOURCES})
    target_include_directories(test_nativedesktop PRIVATE src)
    target_link_libraries(test_nativedesktop PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test ${FRAME_PLATFORM_LIBS})
    add_test(NAME test_nativedesktop COMMAND test_nativedesktop)
endif()

add_executable(test_render_lifetime tests/test_render_lifetime.cpp
    src/core/MpvRenderItem.cpp src/core/PlayerController.cpp src/core/ClipboardCapture.cpp src/core/TrackPreference.cpp src/core/ThumbnailGenerator.cpp)
target_include_directories(test_render_lifetime PRIVATE src)
target_link_libraries(test_render_lifetime PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test mpv)
if(FRAME_ENABLE_RENDER_TESTS)
    add_test(NAME test_render_lifetime COMMAND test_render_lifetime WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_executable(test_startup_presentation tests/test_startup_presentation.cpp
    src/core/MpvRenderItem.cpp src/core/PlayerController.cpp src/core/ClipboardCapture.cpp src/core/TrackPreference.cpp src/core/ThumbnailGenerator.cpp)
target_include_directories(test_startup_presentation PRIVATE src)
target_link_libraries(test_startup_presentation PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test mpv)
if(FRAME_ENABLE_RENDER_TESTS)
    add_test(NAME test_startup_presentation COMMAND test_startup_presentation WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_executable(test_clipboardcapture tests/test_clipboardcapture.cpp
    src/core/ClipboardCapture.cpp)
target_include_directories(test_clipboardcapture PRIVATE src)
target_link_libraries(test_clipboardcapture PRIVATE Qt6::Core Qt6::Gui Qt6::Test mpv)
add_test(NAME test_clipboardcapture COMMAND test_clipboardcapture WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
if(TARGET Qt6::QOffscreenIntegrationPlugin)
    # The staged Windows app deploys only qwindows.dll; this test deliberately
    # uses Qt's isolated offscreen clipboard instead of changing the user's.
    set_tests_properties(test_clipboardcapture PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
endif()

add_executable(test_playback_requests tests/test_playback_requests.cpp
    src/core/PlayerController.cpp src/core/ClipboardCapture.cpp
    src/core/TrackPreference.cpp src/core/ThumbnailGenerator.cpp src/input/ShortcutManager.cpp)
target_include_directories(test_playback_requests PRIVATE src)
target_link_libraries(test_playback_requests PRIVATE Qt6::Core Qt6::Gui Qt6::Test mpv)
# Windows can lose Qt text output from captured pipes; retain assertions in
# a JUnit file as well as the ordinary console logger.
add_test(NAME test_playback_requests COMMAND test_playback_requests -o -,txt
    -o "${CMAKE_CURRENT_BINARY_DIR}/test_playback_requests.xml,junitxml"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

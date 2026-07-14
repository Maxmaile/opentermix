cmake_minimum_required(VERSION 3.18)
project(OpenTermix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

find_package(PkgConfig REQUIRED)
# Embedded terminal emulator widget (LXQt, Qt6 build)
pkg_check_modules(QTERMWIDGET REQUIRED IMPORTED_TARGET qtermwidget6)
# SSH/SFTP library for the built-in file browser
pkg_check_modules(LIBSSH REQUIRED IMPORTED_TARGET libssh)

set(SOURCES
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/SettingsWidget.cpp
    src/app/Icons.cpp
    src/terminal/TerminalWidget.cpp
    src/terminal/TerminalGroup.cpp
    src/terminal/TerminalArea.cpp
    src/terminal/MultiExec.cpp
    src/terminal/ColorSchemeStore.cpp
    src/terminal/ColorSchemeEditorDialog.cpp
    src/sessions/SshConfigParser.cpp
    src/sessions/SessionTreeModel.cpp
    src/sessions/SessionPanel.cpp
    src/sessions/SessionLayout.cpp
    src/sessions/SessionEditorDialog.cpp
    src/sessions/SshArgs.cpp
    src/sessions/TunnelManager.cpp
    src/sessions/TunnelDialog.cpp
    src/sessions/TunnelsTabWidget.cpp
    src/sftp/SftpClient.cpp
    src/sftp/SftpBrowserWidget.cpp
)

set(HEADERS
    src/app/MainWindow.h
    src/app/SettingsWidget.h
    src/app/Icons.h
    src/terminal/TerminalWidget.h
    src/terminal/TerminalGroup.h
    src/terminal/TerminalArea.h
    src/terminal/MultiExec.h
    src/terminal/ColorSchemeStore.h
    src/terminal/ColorSchemeEditorDialog.h
    src/sessions/Session.h
    src/sessions/SshConfigParser.h
    src/sessions/SessionTreeModel.h
    src/sessions/SessionPanel.h
    src/sessions/SessionLayout.h
    src/sessions/SessionEditorDialog.h
    src/sessions/Tunnel.h
    src/sessions/SshArgs.h
    src/sessions/TunnelManager.h
    src/sessions/TunnelDialog.h
    src/sessions/TunnelsTabWidget.h
    src/sftp/SftpClient.h
    src/sftp/SftpBrowserWidget.h
)

add_executable(opentermix ${SOURCES} ${HEADERS} resources/resources.qrc)
target_include_directories(opentermix PRIVATE src)
target_link_libraries(opentermix PRIVATE
    Qt6::Widgets
    PkgConfig::QTERMWIDGET
    PkgConfig::LIBSSH
)

# Localisation. Add new locales by dropping a translations/opentermix_<xx>.ts file
# and listing it here (or run: make update_translations). The compiled .qm files
# are embedded under the ":/i18n" resource prefix and loaded at runtime.
set(OPENTERMIX_TS_FILES
    translations/opentermix_ru.ts
)
qt_add_translations(opentermix
    TS_FILES ${OPENTERMIX_TS_FILES}
    RESOURCE_PREFIX "/i18n"
)

install(TARGETS opentermix RUNTIME DESTINATION bin)
install(FILES debian/opentermix.desktop DESTINATION share/applications)
install(FILES resources/icons/opentermix_256.png DESTINATION share/pixmaps
        RENAME opentermix.png)

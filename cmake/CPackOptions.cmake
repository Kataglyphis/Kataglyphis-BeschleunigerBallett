include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
# Experience shows that explicit package naming can help make it easier to sort
# out potential ABI related issues before they start, while helping you
# track a build to a specific GIT SHA
# Architektur bestimmen (normalisiert), damit sie in den Paketnamen aufgenommen werden kann.
if(NOT DEFINED PROJECT_ARCH)
  if(CMAKE_SYSTEM_PROCESSOR)
    set(PROJECT_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  else()
    execute_process(
      COMMAND uname -m
      OUTPUT_VARIABLE PROJECT_ARCH
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()
string(TOLOWER "${PROJECT_ARCH}" _arch_lc)
set(_ARCH_PKG "${_arch_lc}")
if(_arch_lc STREQUAL "x86_64" OR _arch_lc STREQUAL "amd64")
  set(_ARCH_PKG "x86_64")
elseif(_arch_lc STREQUAL "aarch64" OR _arch_lc STREQUAL "arm64")
  set(_ARCH_PKG "aarch64")
endif()

set(CPACK_PACKAGE_FILE_NAME
    "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${_ARCH_PKG}-${CMAKE_BUILD_TYPE}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
)
set(CPACK_PACKAGE_VENDOR "${AUTHOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_DESCRIPTION "${CMAKE_PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${CMAKE_PROJECT_HOMEPAGE_URL}")
# There is a bug in NSI that does not handle full UNIX paths properly.
# Make sure there is at least one set of four backlashes.
# https://gitlab.kitware.com/cmake/community/-/wikis/doc/cpack/Packaging-With-CPack
set(CPACK_PACKAGE_ICON ${CMAKE_CURRENT_SOURCE_DIR}/images/Engine_logo.png)
set(CPACK_RESOURCE_FILE_WELCOME ${CMAKE_CURRENT_SOURCE_DIR}/docs/packaging/WelcomeFile.txt)
# try to use all cores
set(CPACK_THREADS 0)
set(CPACK_SOURCE_IGNORE_FILES /.git /.*build.*)

set(CPACK_ENABLE_APPIMAGE
    ON
    CACHE BOOL "Enable AppImage package generation on Linux")

set(ENABLE_WIX_PACKAGING
    OFF
    CACHE BOOL "Enable WiX MSI package generation on Windows")

# Windows (egal ob MSVC oder Clang/clang-cl) -> NSIS + WIX Binaries erzeugen
if(WIN32)
  # Standard: NSIS + ZIP. WiX kann optional über ENABLE_WIX_PACKAGING aktiviert werden.
  set(CPACK_GENERATOR "NSIS;ZIP")
  if(ENABLE_WIX_PACKAGING)
    set(CPACK_GENERATOR "${CPACK_GENERATOR};WIX")
  endif()
  # Quellpaket-Format für Windows (optional, sonst ZIP/TGZ). Kann bei Bedarf angepasst werden.
  set(CPACK_SOURCE_GENERATOR "ZIP")

  # Gemeinsame Einstellungen für NSIS
  set(CPACK_NSIS_WELCOME_TITLE "Get ready for epic graphics.")
  set(CPACK_NSIS_FINISH_TITLE "Now you are ready to render :)")
  set(CPACK_NSIS_MUI_HEADERIMAGE ${CMAKE_CURRENT_SOURCE_DIR}/images/Engine_logo.bmp)
  set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP ${CMAKE_CURRENT_SOURCE_DIR}/images/Engine_logo.bmp)
  set(CPACK_NSIS_MUI_UNWELCOMEFINISHPAGE_BITMAP ${CMAKE_CURRENT_SOURCE_DIR}/images/Engine_logo.bmp)
  set(CPACK_NSIS_INSTALLED_ICON_NAME bin/${PROJECT_NAME}.exe)
  set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME}")
  set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME}")
  set(CPACK_NSIS_CONTACT "${CMAKE_PROJECT_HOMEPAGE_URL}")
  set(CPACK_PACKAGE_EXECUTABLES "${PROJECT_NAME}" "${PROJECT_NAME}")
  set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "${PROJECT_NAME}-${PROJECT_VERSION}")
  set(CPACK_NSIS_MENU_LINKS "${CMAKE_PROJECT_HOMEPAGE_URL}" "Homepage for ${PROJECT_NAME}")
  set(CPACK_CREATE_DESKTOP_LINKS "${PROJECT_NAME}")
  set(CPACK_NSIS_URL_INFO_ABOUT "${CMAKE_PROJECT_HOMEPAGE_URL}")
  set(CPACK_NSIS_HELP_LINK "${CMAKE_PROJECT_HOMEPAGE_URL}")
  set(CPACK_NSIS_MUI_ICON ${CMAKE_CURRENT_SOURCE_DIR}/images/faviconNew.ico)
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MODIFY_PATH "ON")

  # Optional: If you need more control over the desktop shortcut, you can use custom NSIS commands
  # This ensures the shortcut has the correct working directory
  set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
      "
    SetOutPath \\\"$INSTDIR\\\\bin\\\"
    CreateShortCut \\\"$DESKTOP\\\\${PROJECT_NAME}.lnk\\\" \\\"$INSTDIR\\\\bin\\\\${PROJECT_NAME}.exe\\\" \\\"\\\" \\\"$INSTDIR\\\\bin\\\\${PROJECT_NAME}.exe\\\" 0 SW_SHOWNORMAL \\\"\\\" \\\"${PROJECT_NAME}\\\"
  ")

  # Optional: Remove the desktop shortcut on uninstall
  set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
      "
    Delete \\\"$DESKTOP\\\\${PROJECT_NAME}.lnk\\\"
  ")

  if(ENABLE_WIX_PACKAGING)
    # WiX spezifische Einstellungen
    # WICHTIG: Diese Upgrade GUID MUSS STABIL BLEIBEN, sonst funktionieren Upgrades/Deinstallationen nicht korrekt.
    # Falls bereits ein Wert existiert, NICHT ändern. Bei erstmaliger Einführung einmalig generieren.
    set(CPACK_WIX_VERSION 4)
    set(CPACK_WIX_UPGRADE_GUID "A8B86F5E-5B3E-4C38-9D7F-4F4923F9E5C2")
    set(CPACK_WIX_PRODUCT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/images/faviconNew.ico)
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "${PROJECT_NAME}")
    set(CPACK_WIX_USE_LONG_FILE_NAMES ON)
    # Optional eigenes Banner/Logo (muss BMP 493x58 bzw. 493x312 sein, wenn gesetzt)
    # set(CPACK_WIX_UI_BANNER ${CMAKE_CURRENT_SOURCE_DIR}/images/your_banner.bmp)
    # set(CPACK_WIX_UI_DIALOG  ${CMAKE_CURRENT_SOURCE_DIR}/images/your_dialog.bmp)

    # License RTF: WiX benötigt echtes RTF. Falls keine LICENSE.rtf vorhanden ist, erzeugen wir eine minimale Dummy-Version,
    # damit der Generator nicht mit 'unsupported WiX License file extension' abbricht (ein häufiger Fall auf CI).
    set(_WIX_LICENSE_RTF "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.rtf")
    if(NOT EXISTS "${_WIX_LICENSE_RTF}")
      file(
        WRITE "${_WIX_LICENSE_RTF}"
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\fs20 This software is licensed under the terms described in the accompanying LICENSE file.\\par}"
      )
    endif()
    set(CPACK_WIX_LICENSE_RTF "${_WIX_LICENSE_RTF}")

    # Beispiel für zusätzliche Einträge in ARP (Add/Remove Programs) - optional
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "${CMAKE_PROJECT_HOMEPAGE_URL}")
    set(CPACK_WIX_PROPERTY_ARPHELPLINK "${CMAKE_PROJECT_HOMEPAGE_URL}")
  endif()

  # Standard-Installationsverzeichnis (unter Program Files)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")

else()
  # Nicht Windows -> Linux / andere UNIX Systeme
  # Source bleibt TGZ; zusätzlich binärer TGZ + (unter Debian/Ubuntu) DEB
  set(CPACK_SOURCE_GENERATOR "TGZ")
  if(UNIX AND NOT APPLE)
    # Binaries als TGZ + DEB ausgeben
    set(CPACK_GENERATOR "TGZ;DEB")
    # Debian/Ubuntu spezifische Felder
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${AUTHOR}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    # Architektur automatisch ermitteln
    # Debian-Architektur (Mapping auf offizielle Deb-Namen)
    if(NOT DEFINED CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
      if(_arch_lc STREQUAL "x86_64" OR _arch_lc STREQUAL "amd64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
      elseif(_arch_lc STREQUAL "aarch64" OR _arch_lc STREQUAL "arm64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
      else()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_arch_lc}")
      endif()
    endif()
    # Abhängigkeiten (einfach gehalten; kann verfeinert werden)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.31)")
    # Automatisches Shlib-Skipping vermeiden falls nötig
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    if(CPACK_ENABLE_APPIMAGE)
      find_program(_APPIMAGETOOL_EXECUTABLE NAMES appimagetool)

      if(NOT _APPIMAGETOOL_EXECUTABLE)
        set(_APPIMAGETOOL_DIR "${CMAKE_BINARY_DIR}/tools")
        file(MAKE_DIRECTORY "${_APPIMAGETOOL_DIR}")

        set(_APPIMAGETOOL_URL "")
        if(_arch_lc STREQUAL "x86_64" OR _arch_lc STREQUAL "amd64")
          set(_APPIMAGETOOL_URL
              "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage")
          set(_APPIMAGETOOL_LOCAL "${_APPIMAGETOOL_DIR}/appimagetool-x86_64.AppImage")
        elseif(_arch_lc STREQUAL "aarch64" OR _arch_lc STREQUAL "arm64")
          set(_APPIMAGETOOL_URL
              "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-aarch64.AppImage")
          set(_APPIMAGETOOL_LOCAL "${_APPIMAGETOOL_DIR}/appimagetool-aarch64.AppImage")
        endif()

        if(_APPIMAGETOOL_URL)
          if(NOT EXISTS "${_APPIMAGETOOL_LOCAL}")
            message(STATUS "appimagetool nicht gefunden. Lade ${_APPIMAGETOOL_URL}")
            file(
              DOWNLOAD "${_APPIMAGETOOL_URL}" "${_APPIMAGETOOL_LOCAL}"
              SHOW_PROGRESS
              STATUS _APPIMAGETOOL_DOWNLOAD_STATUS
              TLS_VERIFY ON)
            list(
              GET
              _APPIMAGETOOL_DOWNLOAD_STATUS
              0
              _APPIMAGETOOL_DOWNLOAD_CODE)
            if(NOT
               _APPIMAGETOOL_DOWNLOAD_CODE
               EQUAL
               0)
              message(
                WARNING
                  "AppImage aktiviert, aber appimagetool Download fehlgeschlagen: ${_APPIMAGETOOL_DOWNLOAD_STATUS}")
            endif()
          endif()

          if(EXISTS "${_APPIMAGETOOL_LOCAL}")
            file(
              CHMOD
              "${_APPIMAGETOOL_LOCAL}"
              PERMISSIONS
              OWNER_READ
              OWNER_WRITE
              OWNER_EXECUTE
              GROUP_READ
              GROUP_EXECUTE
              WORLD_READ
              WORLD_EXECUTE)
            set(_APPIMAGETOOL_EXECUTABLE "${_APPIMAGETOOL_LOCAL}")
          endif()
        else()
          message(
            WARNING
              "CPACK_ENABLE_APPIMAGE=ON, aber Architektur '${_arch_lc}' wird für Auto-Download aktuell nicht unterstützt."
          )
        endif()
      endif()

      if(_APPIMAGETOOL_EXECUTABLE)
        set(_APPIMAGETOOL_WRAPPER "${CMAKE_BINARY_DIR}/tools/appimagetool-wrapper.sh")
        file(WRITE "${_APPIMAGETOOL_WRAPPER}"
             "#!/usr/bin/env sh\nAPPIMAGE_EXTRACT_AND_RUN=1 \"${_APPIMAGETOOL_EXECUTABLE}\" \"$@\"\n")
        file(
          CHMOD
          "${_APPIMAGETOOL_WRAPPER}"
          PERMISSIONS
          OWNER_READ
          OWNER_WRITE
          OWNER_EXECUTE
          GROUP_READ
          GROUP_EXECUTE
          WORLD_READ
          WORLD_EXECUTE)

        list(APPEND CPACK_GENERATOR "AppImage")
        set(CPACK_APPIMAGE_TOOL_EXECUTABLE "${_APPIMAGETOOL_WRAPPER}")
        set(CPACK_APPIMAGE_DESKTOP_FILE "GraphicsEngine.desktop")
        set(CPACK_PACKAGE_ICON "Engine_logo")
        set(CPACK_PACKAGE_EXECUTABLES "GraphicsEngine" "GraphicsEngine")
        message(STATUS "AppImage Packaging aktiviert mit appimagetool: ${_APPIMAGETOOL_EXECUTABLE}")
      else()
        message(
          WARNING "CPACK_ENABLE_APPIMAGE=ON, aber kein appimagetool verfügbar. AppImage Packaging wird übersprungen.")
      endif()
    endif()
  endif()
endif()

include(CPack)

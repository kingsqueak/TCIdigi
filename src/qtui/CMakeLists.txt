cmake_minimum_required(VERSION 3.21)

# Qt shell linked to the autotools fldigi objects. The FLTK binary in src/
# is unchanged. fldigi-main.o is left out; this build compiles main.cxx as
# fldigi_start().
project(fldigi-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum macOS version" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

set(FLDIGI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FLDIGI_LINK_CMAKE "${CMAKE_CURRENT_BINARY_DIR}/fldigi_link.cmake")
execute_process(
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/link_fldigi.py"
          "${FLDIGI_SRC}" "${FLDIGI_LINK_CMAKE}"
  RESULT_VARIABLE FLDIGI_LINK_RC
)
if(NOT FLDIGI_LINK_RC EQUAL 0)
  message(FATAL_ERROR "Could not read the fldigi object list. Build src/fldigi first.")
endif()
include("${FLDIGI_LINK_CMAKE}")

set_source_files_properties(${FLDIGI_MODEM_OBJECTS} PROPERTIES
  EXTERNAL_OBJECT TRUE
  GENERATED TRUE
)

add_library(fldigi_qt_entry OBJECT
  "${FLDIGI_SRC}/main.cxx"
  engine.cxx
)
set_target_properties(fldigi_qt_entry PROPERTIES
  AUTOMOC OFF
  AUTOUIC OFF
  AUTORCC OFF
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED ON
)
target_compile_definitions(fldigi_qt_entry PRIVATE FLDIGI_QT_ENTRY)
target_compile_options(fldigi_qt_entry PRIVATE ${FLDIGI_ENGINE_COMPILE_OPTIONS})
target_include_directories(fldigi_qt_entry PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

qt_add_executable(fldigi-qt
  main.cpp
  theme.h
  theme.cpp
  mainwindow.h
  mainwindow.cpp
  qrzclient.h
  qrzclient.cpp
  waterfallview.h
  waterfallview.cpp
  ${FLDIGI_MODEM_OBJECTS}
)

qt_add_resources(fldigi-qt "qtui_icons"
  PREFIX "/"
  FILES chevron.svg chevron-up.svg icons/TCIdigi.png
)

target_include_directories(fldigi-qt PRIVATE "${FLDIGI_SRC}/include")
target_link_libraries(fldigi-qt PRIVATE fldigi_qt_entry Qt6::Widgets Qt6::Network)
# SHELL keeps -framework and -weak_framework intact. CMake otherwise treats
# those flags as its own keywords and links the framework name as a file.
target_link_options(fldigi-qt PRIVATE "SHELL:${FLDIGI_ENGINE_LINK_LINE}")

file(READ "${FLDIGI_SRC}/include/tcidigi_version.h" TCIDIGI_VERSION_H)
string(REGEX MATCH "#define TCIDIGI_VERSION \"([^\"]+)\"" _tcidigi_ver "${TCIDIGI_VERSION_H}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "TCIDIGI_VERSION is missing from tcidigi_version.h")
endif()
set(TCIDIGI_VERSION "${CMAKE_MATCH_1}")

set_target_properties(fldigi-qt PROPERTIES
  OUTPUT_NAME TCIdigi
  MACOSX_BUNDLE_GUI_IDENTIFIER org.tcidigi.tcidigi
  MACOSX_BUNDLE_BUNDLE_NAME "TCIdigi"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${TCIDIGI_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${TCIDIGI_VERSION}"
)

if(APPLE)
  set_target_properties(fldigi-qt PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE TCIdigi.icns
  )
  # The .app bundle is a directory. zsh reports "permission denied" if that
  # path is executed. This wrapper is the program to run from a shell.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/TCIdigi-run"
"#!/bin/sh\nexec \"$(dirname \"$0\")/TCIdigi.app/Contents/MacOS/TCIdigi\" \"$@\"\n")
  add_custom_command(TARGET fldigi-qt POST_BUILD
    COMMAND chmod 755 "${CMAKE_CURRENT_BINARY_DIR}/TCIdigi-run"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/TCIdigi-run"
            "${CMAKE_CURRENT_BINARY_DIR}/TCIdigi"
    COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/make-icns.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/icons/TCIdigi.png"
            "$<TARGET_BUNDLE_CONTENT_DIR:fldigi-qt>/Resources/TCIdigi.icns"
    VERBATIM
  )
endif()

# cmake --install <build> --prefix /Applications puts TCIdigi.app there.
install(TARGETS fldigi-qt BUNDLE DESTINATION .)

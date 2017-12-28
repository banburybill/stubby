cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

set(CMAKE_VERBOSE_MAKEFILE_ON)

# The following must be set BEFORE doing project() or enable_language().
if (NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type defined; defaulting to 'Debug'")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
    "The type of build. Possible values are: Debug, Release, RelWithDebInfo and MinSizeRel.")
endif()

set(PACKAGE "stubby")
set(PACKAGE_NAME "Stubby")
set(PACKAGE_VERSION "0.2.1")
set(PACKAGE_BUGREPORT "sara@sinodun.com")

set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "${PACKAGE}-${PACKAGE_VERSION}")

include (CheckLibraryExists)
include(CheckIncludeFile)

project(stubby VERSION 0.2.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(PkgConfig)

pkg_check_modules(LIBGETDNS REQUIRED getdns>=1.2.0)
pkg_check_modules(LIBYAML yaml-0.1>=0.1.6)

check_library_exists(getdns getdns_yaml2dict "${LIBGETDNS_LIBRARY_DIRS}" HAVE_GETDNS_YAML2DICT)
if (NOT HAVE_GETDNS_YAML2DICT)
  if (NOT LIBYAML_FOUND)
    message(FATAL_ERROR "Missing required library yaml.")
  endif ()
  set(WITH_YAML 1)
endif ()

check_include_file(assert.h HAVE_ASSERT_H)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(stdarg.h HAVE_STDARG_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(stdio.h HAVE_STDIO_H)
check_include_file(stdlib.h HAVE_STDLIB_H)
check_include_file(string.h HAVE_STRING_H)

check_include_file(os/log.h HAVE_OS_LOG_H)

# Does the compiler accept the "format" attribute?
try_compile(HAVE_ATTR_FORMAT
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/cmake/tests/test_format_attr.c
  )
# Does the compiler accept the "unused" attribute?
try_compile(HAVE_ATTR_UNUSED
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/cmake/tests/test_unused_attr.c
  )

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
  )

# Directories
include(GNUInstallDirs)

if (DEFINED CMAKE_INSTALL_FULL_RUNSTATEDIR)
  set(RUNSTATEDIR "${CMAKE_INSTALL_FULL_RUNSTATEDIR}")
else ()
  set(RUNSTATEDIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/run")
endif ()
install(
  DIRECTORY
  DESTINATION ${RUNSTATEDIR}
  DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
  )

set(STUBBYCONFDIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/stubby")
install(
  DIRECTORY
  DESTINATION ${STUBBYCONFDIR}
  DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
  )

# Platform
if (WIN32 OR MINGW OR MSYS OR CYGWIN)
  set(STUBBY_ON_WINDOWS 1)
  set(ON_WINDOWS 1)
elseif (APPLE)
  set(ON_MACOS 1)
elseif (UNIX)
  set(ON_UNIX 1)
endif ()

# Config file
if (DEFINED ON_WINDOWS)
  set(CONFIG_FILE "stubby.yml.windows")
  # TODO: Generate stubby.yml.windows.
else ()
  set(CONFIG_FILE "stubby.yml.example")
endif()

install(FILES stubby.yml.example
  DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}
  )
install(CODE "
  if (NOT EXISTS \"${STUBBYCONFDIR}/stubby.yml\")
    file(INSTALL \"${CONFIG_FILE}\" DESTINATION \"${STUBBYCONFDIR}/stubby.yml\")
  endif ()"
  )

# Main executable

set(stubby_SOURCES src/stubby.c)
if (DEFINED WITH_YAML)
  list(APPEND stubby_SOURCES src/yaml/convert_yaml_to_json.c src/sldns/sbuffer.c)
endif ()

add_executable(stubby
  ${stubby_SOURCES}
  )

target_include_directories(stubby
  PRIVATE src
  PRIVATE ${LIBGETDNS_INCLUDE_DIRS}
  PRIVATE ${LIBYAML_INCLUDE_DIRS}
  )

target_link_libraries(stubby
  PRIVATE ${LIBGETDNS_LDFLAGS}
  PRIVATE ${LIBYAML_LDFLAGS}
  PRIVATE ${LIBGETDNS_LIBRARIES}
  PRIVATE ${LIBYAML_LIBRARIES}
  )

install(TARGETS stubby
  DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
  )

if (DEFINED ON_MACOS)
  find_library(SECURITY Security)
  if (NOT SECURITY)
    message(FATAL_ERROR "Security framework not found.")
  endif ()

  add_executable(stubby-ui-helper
    macos/stubby-ui-helper.c
    )

  target_link_libraries(stubby-ui-helper ${SECURITY})

  install(PROGRAMS macos/stubby-setdns-macos.sh
    DESTINATION ${CMAKE_INSTALL_FULL_SBINDIR}
    )
endif()

configure_file(${CMAKE_SOURCE_DIR}/cmake/include/cmakeconfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

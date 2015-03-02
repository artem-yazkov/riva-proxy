# ============================================================================ #
# Copyright (C) 2015 RivaSense.                                                #
# ============================================================================ #

project(riva-proxy C)

# CMake configuration:
cmake_minimum_required(VERSION 2.8)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Set default built type to DEBUG:
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
    "Choose the type of build, options are: Debug Release."
    FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)

# Dependencies:

# Compiler options:
set(CMAKE_C_FLAGS "-Werror -Wall -std=gnu99")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Build-time configuration:
include(git-version)
execute_process(COMMAND date "+%Y"
  OUTPUT_VARIABLE PDU_COPYRIGHT_YEAR
  OUTPUT_STRIP_TRAILING_WHITESPACE)
# Generic configuration file:
configure_file(src/config-buildtime.h.in "${CMAKE_BINARY_DIR}/config-buildtime.h")

# Include files:
include_directories(${CMAKE_BINARY_DIR})
include_directories(src)

# Library paths:
include(InstallRequiredSystemLibraries)

add_executable(riva-proxy
  src/aux.c
  src/proxy.c
  src/protocol.c
  src/session.c
  src/config.c
)

target_link_libraries(riva-proxy event mysqlclient)

install(TARGETS riva-proxy DESTINATION bin)

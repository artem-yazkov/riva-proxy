# ============================================================================ #
# Copyright (C) 2015 RivaSense. All rights reserved.                           #
# ============================================================================ #
#
# Set version based on git information and current state of source directory.
#
# The module defines the following variables:
#   GIT_COMMIT_NUM - The number of the last commit in HEAD
#   GIT_SOURCE_SHA - Short SHA sign of the last commit in HEAD
#   GIT_DIRTY      - ".dirty" for dirty repository, "" otherwise
#   GIT_VERSION    - Combination of GIT_COMMIT_NUM, GIT_SOURCE_SHA, GIT_DIRTY

find_package(Git)

set(GIT_COMMIT_NUM  0)
set(GIT_COMMIT_SHA  "")
set(GIT_DIRTY       "")
set(GIT_VERSION     "")

if(NOT (GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git"))
    return()
endif()

execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT_NUM
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

execute_process(COMMAND ${GIT_EXECUTABLE} status --untracked-files=no --porcelain
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

if(NOT "${GIT_STATUS}" STREQUAL "")
    set(GIT_DIRTY ".dirty")
endif()

set(GIT_VERSION "${GIT_COMMIT_NUM}.${GIT_COMMIT_SHA}${GIT_DIRTY}")

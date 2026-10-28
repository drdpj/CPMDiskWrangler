# Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler. GPL v3 or later.
#
# The program's version, from git when CMake configures - the same rules as version.pri: "git describe --tags
# --always --dirty", or "unknown" without git, unless given with -DAPP_VERSION=v1.0.0. Sets APP_VERSION and,
# when it starts with x.y.z, APP_VERSION_NUMBER. Re-run CMake's configure step to pick up a new commit or tag.

if(NOT APP_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --always --dirty
                        OUTPUT_VARIABLE APP_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
endif()
if(NOT APP_VERSION)
    set(APP_VERSION unknown)
endif()
set(APP_VERSION_NUMBER "")
if(APP_VERSION MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(APP_VERSION_NUMBER "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
endif()
message(STATUS "${PROJECT_NAME} version: ${APP_VERSION}")

# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)
project(nuka_protocol_t8 C)
set(CMAKE_C_STANDARD 11)
get_filename_component(NUKA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(IDF_SOURCE "${NUKA_ROOT}/build/esp-idf" CACHE PATH "ESP-IDF checkout")
add_executable(nuka_protocol_t8 test_t8.c ../proto.c ../vm_topics.c
    "${IDF_SOURCE}/components/json/cJSON/cJSON.c"
    "${IDF_SOURCE}/components/unity/unity/src/unity.c")
target_include_directories(nuka_protocol_t8 PRIVATE ..
    "${IDF_SOURCE}/components/json/cJSON"
    "${IDF_SOURCE}/components/unity/unity/src")
target_link_libraries(nuka_protocol_t8 PRIVATE m)
target_compile_definitions(nuka_protocol_t8 PRIVATE NUKA_EXAMPLES="${NUKA_ROOT}/protocol/examples/device")
target_compile_options(nuka_protocol_t8 PRIVATE -Wall -Wextra -Werror)
enable_testing()
add_test(NAME T8 COMMAND nuka_protocol_t8)

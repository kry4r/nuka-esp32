# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(PROJECT_VER "0.1.0")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/idf_component.yml"
               "${CMAKE_CURRENT_SOURCE_DIR}/main/idf_component.yml" COPYONLY)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nuka-esp32)

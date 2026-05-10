# Caracal - Ultra-lightweight browser for ESP32-S3
# Named after the caracal: a smaller, lighter wild cat than the lynx.
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(caracal)

cmake_minimum_required(VERSION 3.16)

# Firmware version. Shows in the boot log ("App version") and in the image's
# app description; keep it in step with the git tag (v1.0.0 <-> 1.0.0).
set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(photoframe)

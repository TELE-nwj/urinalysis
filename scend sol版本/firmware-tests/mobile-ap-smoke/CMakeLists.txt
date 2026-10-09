cmake_minimum_required(VERSION 3.22)
# This isolated trial always targets the ESP32-S3, regardless of defaults in
# other repository projects. Only the reusable WiFi/link drivers are included.
set(IDF_TARGET "esp32s3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(mobile_ap_smoke)

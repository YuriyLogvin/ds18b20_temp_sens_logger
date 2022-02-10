# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
#  "esp-idf-lib/esp_idf_lib_helpers" "esp-idf-lib/onewire" "esp-idf-lib/ds18x20" C:/Users/banz/esp/esp-idf/examples/common_components/protocol_examples_common

set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common "esp-idf-lib/esp_idf_lib_helpers" "esp-idf-lib/onewire" "esp-idf-lib/ds18x20")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(file_server)

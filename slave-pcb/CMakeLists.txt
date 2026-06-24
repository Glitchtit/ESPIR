cmake_minimum_required(VERSION 3.16)
# Custom-PCB slave: same firmware as slave/ plus the RGB status LED (espir_led).
# Pull in the shared components/ that master and slave both use.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espir_slave_pcb)

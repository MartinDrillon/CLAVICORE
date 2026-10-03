cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to the Pico SDK directory.")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(clavier C CXX ASM)

pico_sdk_init()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PINMAP_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/../../Netlist/netlist_to_pinmap.py)
set(PINMAP_NETLIST ${CMAKE_CURRENT_LIST_DIR}/../../Électrique/Clavier/Clavier_drive_1/Clavier_drive_1.xml)
set(PINMAP_HEADER ${CMAKE_CURRENT_LIST_DIR}/src/hal/pinmap.generated.h)

add_custom_target(clavier_pinmap
    COMMAND ${Python3_EXECUTABLE} ${PINMAP_SCRIPT} ${PINMAP_NETLIST}
            --ref A1 --board clavier
            --yaml-out ${CMAKE_CURRENT_BINARY_DIR}/pinmap.generated.yaml
            --header-out ${PINMAP_HEADER}
    DEPENDS ${PINMAP_SCRIPT} ${PINMAP_NETLIST}
    COMMENT "Generating keyboard pin map"
    VERBATIM
)

add_executable(clavier
    src/main.c
    src/keycode.c
    src/keymap.c
    src/matrix_scan.c
    src/midi.c
    src/note_state.c
    src/usb_descriptors.c
)

# Pinmap regeneration is manual so normal firmware builds do not require KiCad exports.
target_include_directories(clavier PRIVATE src)
target_link_libraries(clavier PRIVATE pico_stdlib hardware_gpio tinyusb_device tinyusb_board)

pico_enable_stdio_uart(clavier 0)
pico_add_extra_outputs(clavier)
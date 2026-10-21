cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to the Pico SDK directory.")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(clavier C CXX ASM)

pico_sdk_init()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Mode diagnostic (IPROPI_RAW + IPROPI_OHM) : actif par défaut pendant le
# bring-up (ADR-0009 §7). À repasser à OFF en configuration finale.
option(CLAVIER_SELFTEST_CALIBRATION "Emet la calibration IPROPI_RAW/OHM lors des auto-tests demandes" ON)
# Console de bring-up sur CDC (t/c/d/m/?) : ADR-0009 §9 livraison B.
option(CLAVIER_CDC_CONSOLE "Active la console de bring-up sur le port CDC" ON)

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
    src/bringup_console.c
    src/can_link.c
    src/commissioning.c
    src/keycode.c
    src/keymap.c
    src/matrix_scan.c
    src/midi.c
    src/note_state.c
    src/diag_log.c
    src/usb_log.c
    src/usb_descriptors.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/protocol/can_protocol.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/protocol/commissioning_table.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/protocol/solenoid_profile.c
)

# can2040 (ADR-0002) : sous-module git épinglé v1.7.0. Jamais de copie fabriquée.
set(CAN2040_DIR ${CMAKE_CURRENT_LIST_DIR}/../common/can2040)
if(NOT EXISTS ${CAN2040_DIR}/src/can2040.c)
    message(FATAL_ERROR
        "Sous-module can2040 absent (${CAN2040_DIR}).\n"
        "Depuis la racine du depot, lancer :\n"
        "  git submodule update --init --recursive\n"
        "ou, s'il n'est pas encore declare :\n"
        "  git submodule add https://github.com/KevinOConnor/can2040 firmware/common/can2040\n"
        "  git -C firmware/common/can2040 checkout v1.7.0")
endif()
add_library(can2040 STATIC ${CAN2040_DIR}/src/can2040.c)
target_include_directories(can2040 PUBLIC ${CAN2040_DIR}/src)
target_compile_definitions(can2040 PUBLIC PICO_RP2350)  # exigé par can2040 pour la famille RP2350
target_compile_options(can2040 PRIVATE -O2)             # can2040 exige -O2 minimum
target_link_libraries(can2040 PUBLIC pico_stdlib hardware_pio hardware_irq hardware_clocks)
if(TARGET cmsis_core)
    target_link_libraries(can2040 PUBLIC cmsis_core)
endif()

# Pinmap regeneration is manual so normal firmware builds do not require KiCad exports.
# shared/protocol : source unique du protocole CAN et du format de table UID.
target_include_directories(clavier PRIVATE src ${CMAKE_CURRENT_LIST_DIR}/../../shared/protocol)
target_link_libraries(clavier PRIVATE pico_stdlib hardware_gpio hardware_irq hardware_sync hardware_clocks hardware_flash pico_flash can2040 tinyusb_device tinyusb_board)
if(CLAVIER_SELFTEST_CALIBRATION)
    target_compile_definitions(clavier PRIVATE CLAVIER_SELFTEST_CALIBRATION=1)
else()
    target_compile_definitions(clavier PRIVATE CLAVIER_SELFTEST_CALIBRATION=0)
endif()
if(CLAVIER_CDC_CONSOLE)
    target_compile_definitions(clavier PRIVATE CLAVIER_CDC_CONSOLE=1)
else()
    target_compile_definitions(clavier PRIVATE CLAVIER_CDC_CONSOLE=0)
endif()

pico_enable_stdio_uart(clavier 0)
pico_add_extra_outputs(clavier)
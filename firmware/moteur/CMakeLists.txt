cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to the Pico SDK directory.")
endif()

# Carte custom RP2354B (QFN80) : aucun board Pico-SDK officiel ne correspond
# exactement à ce jour (cf. boards/moteur_rp2354b.h pour le détail de
# l'incertitude technique non résolue). PICO_BOARD/PICO_BOARD_HEADER_DIRS
# doivent être positionnés avant l'inclusion de pico_sdk_import.cmake.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD moteur_rp2354b)
endif()
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(moteur C CXX ASM)

pico_sdk_init()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PINMAP_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/../../Netlist/netlist_to_pinmap.py)
set(PINMAP_NETLIST ${CMAKE_CURRENT_LIST_DIR}/../../Netlist/Solenoide_drive.xml)
set(PINMAP_HEADER ${CMAKE_CURRENT_LIST_DIR}/src/hal/pinmap.generated.h)

add_custom_target(moteur_pinmap
    COMMAND ${Python3_EXECUTABLE} ${PINMAP_SCRIPT} ${PINMAP_NETLIST}
            --ref IC1 --board moteur
            --yaml-out ${CMAKE_CURRENT_BINARY_DIR}/pinmap.generated.yaml
            --header-out ${PINMAP_HEADER}
    DEPENDS ${PINMAP_SCRIPT} ${PINMAP_NETLIST}
    COMMENT "Generating moteur pin map"
    VERBATIM
)

add_executable(moteur
    src/main.c
)

# Pinmap regeneration is manual; config.h fixe les rôles logiques verrouillés par
# l'ADR-0001 (IMP-001) et ne doit jamais être dérivé automatiquement du header généré.
target_include_directories(moteur PRIVATE src)
target_link_libraries(moteur PRIVATE pico_stdlib hardware_gpio hardware_pwm)

pico_enable_stdio_uart(moteur 0)
pico_add_extra_outputs(moteur)

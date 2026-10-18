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

option(MOTEUR_ENABLE_CLOCK_TEST "Compiler la feature clock-test-mode1 (diagnostic, GPIO41)" ON)

add_executable(moteur
    src/main.c
    src/board_identity.c
    src/can_link.c
    src/ipropi_selftest.c
    src/pwm_hal.c
    src/solenoid_driver.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/protocol/can_protocol.c
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

# Quand OFF : clock_test.c n'est ni compilé ni lié, clock_test.h fournit des stubs.
if(MOTEUR_ENABLE_CLOCK_TEST)
    target_sources(moteur PRIVATE src/clock_test.c)
    target_compile_definitions(moteur PRIVATE MOTEUR_ENABLE_CLOCK_TEST=1)
else()
    target_compile_definitions(moteur PRIVATE MOTEUR_ENABLE_CLOCK_TEST=0)
endif()

# Pinmap regeneration is manual; config.h fixe les rôles logiques verrouillés par
# l'ADR-0001 (IMP-001) et ne doit jamais être dérivé automatiquement du header généré.
# Le Moteur n'a JAMAIS le chemin de firmware/clavier/src dans ses includes (invariant 2 :
# uid_position.h reste côté Clavier). shared/protocol = protocole CAN, source unique.
target_include_directories(moteur PRIVATE src ${CMAKE_CURRENT_LIST_DIR}/../../shared/protocol)
target_link_libraries(moteur PRIVATE pico_stdlib hardware_gpio hardware_pwm hardware_irq hardware_sync hardware_clocks hardware_adc pico_unique_id can2040)

pico_enable_stdio_uart(moteur 0)
pico_add_extra_outputs(moteur)

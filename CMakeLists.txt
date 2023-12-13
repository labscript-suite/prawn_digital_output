cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../pico-sdk)

include(pico_sdk_import.cmake)

project(prawn_do_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(prawn_do_project
        prawn_do.c
		fast_serial.c
        )

pico_generate_pio_header(prawn_do_project
${CMAKE_CURRENT_LIST_DIR}/prawn_do.pio
)

target_include_directories(prawn_do_project PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(prawn_do_project
    pico_multicore
    pico_stdlib
    pico_unique_id
    hardware_dma
    hardware_pio
    tinyusb_device
    tinyusb_board
)

pico_add_extra_outputs(prawn_do_project)

cmake_minimum_required(VERSION 3.13)


include(pico_sdk_import.cmake)

project(prawn_do_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(prawn_do_project
        prawn_do.c
		serial.c
        )

pico_generate_pio_header(prawn_do_project
${CMAKE_CURRENT_LIST_DIR}/prawn_do.pio
)

target_link_libraries(prawn_do_project
    pico_stdlib
    hardware_dma
    hardware_pio
)

pico_enable_stdio_usb(prawn_do_project 1)
pico_enable_stdio_uart(prawn_do_project 0)

pico_add_extra_outputs(prawn_do_project)

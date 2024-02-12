add_executable(prawn_do
        prawn_do.c
		fast_serial.c
        )

pico_generate_pio_header(prawn_do
${CMAKE_CURRENT_LIST_DIR}/prawn_do.pio
)

# add local includes for fast_serial
target_include_directories(prawn_do PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# pull in pico stdlib
target_link_libraries(prawn_do
    pico_multicore
    pico_stdlib
    pico_unique_id
    hardware_dma
    hardware_pio
    tinyusb_device
    tinyusb_board
)

# create map/bin/hex/uf2 files
pico_add_extra_outputs(prawn_do)

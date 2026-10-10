# Host tests of the hardware-independent code: the TMP-v2 slave and its MAC
# driver under the real IP stack, the load control stream and the link check
# on top of them, and the image format shared with mkimage.py.

vedc_stack(stack_host ON)

# The 485 master and fake hardware both link tests share.
add_library(master STATIC master.c ${CMAKE_SOURCE_DIR}/common/link485.c)
target_include_directories(master PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/common)
target_link_libraries(master PUBLIC stack_host)
target_compile_options(master PRIVATE -Wall -Wextra)

add_executable(test_link test_link.c)
target_link_libraries(test_link PRIVATE master)
target_compile_options(test_link PRIVATE -Wall -Wextra)
add_test(NAME link COMMAND test_link)

add_executable(test_loadctl test_loadctl.c ${CMAKE_SOURCE_DIR}/app/loadctl.c)
target_include_directories(test_loadctl PRIVATE ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(test_loadctl PRIVATE master)
target_compile_options(test_loadctl PRIVATE -Wall -Wextra)
add_test(NAME loadctl COMMAND test_loadctl)

add_executable(test_linkcheck test_linkcheck.c ${CMAKE_SOURCE_DIR}/common/linkcheck.c)
target_link_libraries(test_linkcheck PRIVATE master)
target_compile_options(test_linkcheck PRIVATE -Wall -Wextra)
add_test(NAME linkcheck COMMAND test_linkcheck)

# An image stamped by mkimage.py must pass the firmware's own check.
add_custom_command(OUTPUT sample.bin
    COMMAND ${Python3_EXECUTABLE} -c
        "import struct; d = bytearray(range(256)) * 5 + b'xyz'; struct.pack_into('<I', d, 28, 0x41444556); open('sample.raw', 'wb').write(d)"
    COMMAND ${Python3_EXECUTABLE} ${MKIMAGE} image sample.raw sample.bin --slot-size 0x1000
    DEPENDS ${MKIMAGE} VERBATIM)
add_custom_target(sample_image ALL DEPENDS sample.bin)

add_executable(test_image test_image.c ${CMAKE_SOURCE_DIR}/common/image.c)
target_include_directories(test_image PRIVATE ${CMAKE_SOURCE_DIR}/common)
target_compile_options(test_image PRIVATE -Wall -Wextra)
add_test(NAME image COMMAND test_image ${CMAKE_CURRENT_BINARY_DIR}/sample.bin)

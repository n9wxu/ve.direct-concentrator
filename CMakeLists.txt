cmake_minimum_required(VERSION 3.20)
project(vedirect_concentrator C)

# Firmware (stage0, bootloader, application):
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake && cmake --build build
# Host tests of the portable parts:
#   cmake -B build-host && cmake --build build-host && ctest --test-dir build-host

# Reported by "status" and stored in each image. Bump for every release.
set(VEDC_VERSION 10 CACHE STRING "Version number stored in the images")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(cmake/deps.cmake)

set(MKIMAGE ${CMAKE_SOURCE_DIR}/tools/mkimage.py)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_subdirectory(test)
    return()
endif()

# ── Firmware ──────────────────────────────────────────────────────────

# min-pagesize: the flash starts at address 0, so low addresses are not null
# pointers plus an offset.
add_compile_options(-mcpu=cortex-m0plus -mthumb -ffunction-sections -fdata-sections
                    -Wall -Wextra --param=min-pagesize=0)
add_link_options(-mcpu=cortex-m0plus -mthumb -nostartfiles --specs=nano.specs
                 --specs=nosys.specs -Wl,--gc-sections -L${CMAKE_SOURCE_DIR}/common)

add_library(samd21 INTERFACE)
target_include_directories(samd21 SYSTEM INTERFACE
    ${cmsis_atmel_SOURCE_DIR}/CMSIS-Atmel/CMSIS/Device/ATMEL
    ${cmsis_atmel_SOURCE_DIR}/CMSIS-Atmel/CMSIS/Device/ATMEL/samd21/include
    ${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include)
target_compile_definitions(samd21 INTERFACE __SAMD21G18A__)

vedc_stack(stack_boot OFF)
vedc_stack(stack_app  ON)

# Link, convert to a binary, and stamp the length and CRC into it.
function(vedc_image target script slot_size)
    target_link_options(${target} PRIVATE -T${script}
        -Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map)
    set_target_properties(${target} PROPERTIES SUFFIX .elf LINK_DEPENDS
        "${script};${CMAKE_SOURCE_DIR}/common/sections.ld")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${target}.raw
        COMMAND ${Python3_EXECUTABLE} ${MKIMAGE} image ${target}.raw ${target}.bin
                --slot-size ${slot_size}
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        BYPRODUCTS ${target}.raw ${target}.bin
        VERBATIM)
endfunction()

# stage0: never updated in the field.
add_executable(vedc-stage0 stage0/main.c common/image.c common/nvm.c)
target_include_directories(vedc-stage0 PRIVATE common)
target_link_libraries(vedc-stage0 PRIVATE samd21)
target_link_options(vedc-stage0 PRIVATE -T${CMAKE_SOURCE_DIR}/stage0/stage0.ld)
set_target_properties(vedc-stage0 PROPERTIES SUFFIX .elf)
add_custom_command(TARGET vedc-stage0 POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:vedc-stage0> vedc-stage0.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vedc-stage0>
    BYPRODUCTS vedc-stage0.bin VERBATIM)

set(COMMON_SOURCES
    common/startup.c common/clock.c common/uart.c common/nvm.c common/image.c
    common/link485.c common/fota.c)

add_executable(vedc-boot boot/main.c ${COMMON_SOURCES})
target_include_directories(vedc-boot PRIVATE common)
target_compile_definitions(vedc-boot PRIVATE IMAGE_MAGIC=IMAGE_MAGIC_BOOT
                           IMAGE_VERSION=${VEDC_VERSION})
target_link_libraries(vedc-boot PRIVATE samd21 stack_boot)
vedc_image(vedc-boot ${CMAKE_SOURCE_DIR}/boot/boot.ld 0x4000)

set(tusb ${tinyusb_SOURCE_DIR}/src)
add_library(tinyusb STATIC
    ${tusb}/tusb.c ${tusb}/common/tusb_fifo.c ${tusb}/device/usbd.c
    ${tusb}/device/usbd_control.c ${tusb}/class/cdc/cdc_device.c
    ${tusb}/portable/microchip/samd/dcd_samd.c)
target_include_directories(tinyusb PUBLIC ${tusb} app)
target_link_libraries(tinyusb PUBLIC samd21)
target_compile_options(tinyusb PRIVATE -w)

add_executable(vedc-app app/main.c app/veports.c app/control.c app/loadctl.c app/loads.c
               app/tcplink.c app/usbmon.c app/vemock.c app/ledtest.c app/usb_descriptors.c
               common/linkcheck.c ${COMMON_SOURCES})
target_include_directories(vedc-app PRIVATE common app)
target_compile_definitions(vedc-app PRIVATE IMAGE_MAGIC=IMAGE_MAGIC_APP
                           IMAGE_VERSION=${VEDC_VERSION})
target_link_libraries(vedc-app PRIVATE samd21 stack_app tinyusb)
vedc_image(vedc-app ${CMAKE_SOURCE_DIR}/app/app.ld 0x37800)

# Everything in one file, for the first programming over SWD.
add_custom_target(factory ALL
    COMMAND ${Python3_EXECUTABLE} ${MKIMAGE} factory vedc-factory.bin
            vedc-stage0.bin vedc-boot.bin vedc-app.bin
    DEPENDS vedc-stage0 vedc-boot vedc-app
    BYPRODUCTS vedc-factory.bin VERBATIM)

# Atmel-ICE (CMSIS-DAP) over SWD. "at91samd bootloader 0" lifts the BOOTPROT
# fuse an Arduino bootloader leaves set; the chip erase clears the staging
# slot too, which the factory image does not reach — a bootloader left staged
# there would otherwise be installed over the new one. See README.md.
add_custom_target(flash
    COMMAND openocd -f interface/cmsis-dap.cfg -c "transport select swd"
            -f target/at91samdXX.cfg
            -c "init" -c "reset halt" -c "at91samd bootloader 0" -c "at91samd chip-erase"
            -c "program vedc-factory.bin 0x00000000 verify" -c "reset run" -c "shutdown"
    DEPENDS factory USES_TERMINAL VERBATIM)

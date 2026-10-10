include(FetchContent)

# Sources only: each is compiled here with this project's own settings, so the
# packages' build files are not used (SOURCE_SUBDIR names a directory that does
# not exist).

FetchContent_Declare(smallest_tcp
    GIT_REPOSITORY https://github.com/n9wxu/smallest_tcp.git
    GIT_TAG        v0.1.10
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _sources_only)
FetchContent_MakeAvailable(smallest_tcp)

if(CMAKE_CROSSCOMPILING)
    FetchContent_Declare(cmsis_atmel
        GIT_REPOSITORY https://github.com/arduino/ArduinoModule-CMSIS-Atmel.git
        GIT_TAG        v1.2.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _sources_only)
    FetchContent_Declare(cmsis_core
        GIT_REPOSITORY https://github.com/ARM-software/CMSIS_6.git
        GIT_TAG        v6.1.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _sources_only)
    FetchContent_Declare(tinyusb
        GIT_REPOSITORY https://github.com/hathach/tinyusb.git
        GIT_TAG        0.18.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _sources_only)
    FetchContent_MakeAvailable(cmsis_atmel cmsis_core tinyusb)
endif()

# The IP stack, built once per configuration: the settings change the layout
# of net_t, so the bootloader (no TCP) and the application cannot share one.
function(vedc_stack name with_tcp)
    set(src ${smallest_tcp_SOURCE_DIR}/src)
    add_library(${name} STATIC
        ${src}/net.c ${src}/net_cksum.c ${src}/net_text.c ${src}/eth.c ${src}/arp.c ${src}/ipv4.c
        ${src}/icmp.c ${src}/udp.c ${src}/tftp.c)
    if(with_tcp)
        target_sources(${name} PRIVATE ${src}/tcp.c ${src}/tcp_buf_saw.c)
    endif()
    target_include_directories(${name} PUBLIC ${smallest_tcp_SOURCE_DIR}/include)
    target_compile_definitions(${name} PUBLIC
        NET_USE_IPV4=1 NET_USE_IPV6=0 NET_USE_UDP=1 NET_USE_TCP=$<BOOL:${with_tcp}>
        NET_MAX_MCAST_GROUPS=0)
endfunction()

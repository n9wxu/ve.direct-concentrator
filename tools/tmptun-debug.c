/*
 * tmptun-debug — recon's tunnel master (trailer-monitor recon/tmptun/tmptun.c,
 * 3f90f35) with every poll accounted for, to find out what the link does
 * under the real master's timing. Runs in tmptun's place for a while:
 *
 *   sudo systemctl stop tmptun
 *   sudo ./tmptun-debug [seconds [min_gap_us]]     # default 30 s, no gap
 *   sudo systemctl start tmptun
 *
 * It prints a line for each poll that timed out (what the poll and the one
 * before it carried, how long after the previous exchange it went out, the
 * bytes heard, if any) and for each reply that came back slower than its
 * length explains, then the totals. min_gap_us holds a poll that follows a
 * frame-carrying exchange that long after it (tmptun sends it at once and
 * naps 0.5 ms only when idle): a timeout that a gap removes is a window in
 * which the unit cannot hear the bus.
 *
 * Build on recon, with this repo's common/tmp_v2.h (the same file as
 * trailer-monitor's protocol/tmp_v2.h) beside it or on the include path:
 *
 *   gcc -O2 -Wall -Wextra -std=c11 -I. -o tmptun-debug tmptun-debug.c
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include "tmp_v2.h"

#define SERIAL_DEVICE       "/dev/ttyAMA3"
#define TAP_DEVICE          "tap0"
#define REPLY_TIMEOUT_MS    100  /* a full-MTU poll + reply is ~62ms at 500 kbaud */
#define IDLE_NAP_NS         (500L * 1000L)   /* 0.5ms when both directions quiet */

static long monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static int open_serial(void)
{
    int fd = open(SERIAL_DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open " SERIAL_DEVICE);
        return -1;
    }
    struct termios settings;
    tcgetattr(fd, &settings);
    cfmakeraw(&settings);
    cfsetispeed(&settings, B500000);
    cfsetospeed(&settings, B500000);
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &settings);
    return fd;
}

static int open_tap(void)
{
    int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/net/tun");
        return -1;
    }
    struct ifreq request;
    memset(&request, 0, sizeof request);
    request.ifr_flags = IFF_TAP | IFF_NO_PI;
    snprintf(request.ifr_name, IFNAMSIZ, "%s", TAP_DEVICE);
    if (ioctl(fd, TUNSETIFF, &request) < 0) {
        perror("TUNSETIFF");
        close(fd);
        return -1;
    }
    return fd;
}

static uint16_t take_frame_from_tap(int tap_fd, uint8_t *out)
{
    ssize_t count = read(tap_fd, out, TMP_ETHERNET_FRAME_MAX);
    return (count > 0) ? (uint16_t)count : 0u;
}


/* ── instrumented master ────────────────────────────────────────────── */

static long monotonic_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000L + now.tv_nsec / 1000L;
}

static struct {
    unsigned long polls, replies, timeouts, crcfails, other_frames;
} stats;
static uint8_t  heard[4096];
static size_t   heard_length;
static long     reply_us;         /* poll written -> reply complete */
static long     first_byte_us;    /* poll written -> first byte back */

static bool poll_for_reply(int serial_fd, uint8_t sequence,
                           const uint8_t *down_frame, uint16_t down_length,
                           struct tmp_scanner *scanner, struct tmp_frame *reply)
{
    static uint8_t wire[TMP_FRAME_MAX];
    const size_t wire_length = tmp_frame_build(wire, TMP_POLL, TMP_ADDR_COLLECTOR, sequence,
                                               tmp_flags(false, 0u), down_frame, down_length);
    if (wire_length == 0u) {
        return false;
    }

    tcflush(serial_fd, TCIFLUSH);
    tmp_scanner_reset(scanner);
    heard_length = 0u;
    first_byte_us = -1;
    stats.polls++;
    const long sent_us = monotonic_us();
    if (write(serial_fd, wire, wire_length) < 0) {
        return false;
    }

    const long deadline = monotonic_ms() + REPLY_TIMEOUT_MS;
    for (;;) {
        const long remaining = deadline - monotonic_ms();
        if (remaining <= 0) {
            stats.timeouts++;
            return false;
        }
        struct pollfd watch = { .fd = serial_fd, .events = POLLIN, .revents = 0 };
        if (poll(&watch, 1, (int)remaining) <= 0) {
            stats.timeouts++;
            return false;
        }
        uint8_t chunk[256];
        const ssize_t got = read(serial_fd, chunk, sizeof chunk);
        if (got > 0 && first_byte_us < 0) {
            first_byte_us = monotonic_us() - sent_us;
        }
        for (ssize_t index = 0; index < got; index++) {
            if (heard_length < sizeof heard) {
                heard[heard_length++] = chunk[index];
            }
            const enum tmp_scan_result result = tmp_scanner_push(scanner, chunk[index], reply);
            if (result == SCAN_CRC_FAILED) {
                stats.crcfails++;
            } else if (result == SCAN_FRAME_READY) {
                if (reply->type == TMP_NET) {
                    stats.replies++;
                    reply_us = monotonic_us() - sent_us;
                    return true;
                }
                stats.other_frames++;
            }
        }
    }
}

int main(int argc, char **argv)
{
    const int seconds = (argc > 1) ? atoi(argv[1]) : 30;
    const long min_gap_us = (argc > 2) ? atol(argv[2]) : 0;
    const int serial_fd = open_serial();
    const int tap_fd = open_tap();
    if (serial_fd < 0 || tap_fd < 0) {
        return 1;
    }
    fprintf(stderr, "tmptun-debug on %s <-> %s @ %u for %d s\n", SERIAL_DEVICE, TAP_DEVICE,
            TMP_V2_BAUD, seconds);

    struct tmp_scanner scanner = { 0 };
    tmp_scanner_reset(&scanner);
    static uint8_t down_frame[TMP_ETHERNET_FRAME_MAX];
    struct tmp_frame reply = { 0 };
    uint8_t sequence = 0u;
    const long end_us = monotonic_us() + seconds * 1000000L;
    long last_reply_end_us = 0;
    uint16_t last_down = 0u, last_up = 0u;
    bool last_napped = false;
    unsigned long slow_replies = 0;

    while (monotonic_us() < end_us) {
        const uint16_t down_length = take_frame_from_tap(tap_fd, down_frame);
        uint16_t up_length = 0u;
        while (!last_napped && last_reply_end_us
               && monotonic_us() - last_reply_end_us < min_gap_us) {
        }
        const long start_us = monotonic_us();
        const long gap_us = last_reply_end_us ? start_us - last_reply_end_us : -1;
        const bool ok = poll_for_reply(serial_fd, sequence, down_length ? down_frame : NULL,
                                       down_length, &scanner, &reply);
        if (ok && reply.length > 0u) {
            (void)!write(tap_fd, reply.payload, reply.length);
            up_length = reply.length;
        }
        if (!ok) {
            printf("TIMEOUT seq=%u down=%u | prev: down=%u up=%u napped=%d gap=%ldus | heard %zu B first@%ldus:",
                   sequence, down_length, last_down, last_up, last_napped, gap_us,
                   heard_length, first_byte_us);
            for (size_t i = 0; i < heard_length && i < 24; i++) {
                printf(" %02x", heard[i]);
            }
            printf("\n");
            fflush(stdout);
        } else if (reply_us > 20000 + (long)(down_length + reply.length) * 20) {
            slow_replies++;
            printf("SLOW seq=%u down=%u up=%u reply=%ldus first@%ldus gap=%ldus\n",
                   sequence, down_length, reply.length, reply_us, first_byte_us, gap_us);
            fflush(stdout);
        }
        last_reply_end_us = monotonic_us();
        last_down = down_length;
        last_up = up_length;
        sequence++;
        last_napped = (down_length == 0u && up_length == 0u);
        if (last_napped) {
            const struct timespec nap = { .tv_sec = 0, .tv_nsec = IDLE_NAP_NS };
            nanosleep(&nap, NULL);
        }
    }
    printf("min_gap %ldus: polls %lu replies %lu timeouts %lu crcfails %lu other %lu slow %lu\n",
           min_gap_us, stats.polls, stats.replies, stats.timeouts, stats.crcfails, stats.other_frames,
           slow_replies);
    return 0;
}

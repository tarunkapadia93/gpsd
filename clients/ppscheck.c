/*
 * Watch a specified serial port for transitions that might be 1PPS.
 *
 * Each output line is the second and nanosecond parts of a timestamp
 * followed by the names of handshake signals then asserted.  Off
 * transitions may generate lines with no signals asserted.
 *
 * If you don't see output within a second, use gpsmon or some other
 * equivalent tool to check that your device has satellite lock and is
 * getting fixes before giving up on the possibility of 1PPS.
 *
 * Also, check your cable. Cheap DB9 to DB9 cables such as those
 * issued with UPSes often carry TXD/RXD/GND only, omitting handshake
 * lines such as DCD.  Suspect this especially if the cable jacket
 * looks too skinny to hold more than three leads!
 *
 * This code requires only ANSI/POSIX. If it doesn't compile and run
 * on your Unix there is something very wrong with your Unix.
 *
 * This code by ESR, Copyright 2013, under BSD terms.
 * This file is Copyright 2013 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <errno.h>
#include <fcntl.h>      /* needed for open() and friends */
#ifdef HAVE_GETOPT_LONG
       #include <getopt.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "timespec.h"

struct assoc {
    int mask;
    char *string;
};

/*
 * Possible pins for PPS: DCD, CTS, RI, DSR. Pinouts:
 *
 * DB9  DB25  Name      Full name
 * ---  ----  ----      --------------------
 *  3     2    TXD  --> Transmit Data
 *  2     3    RXD  <-- Receive Data
 *  7     4    RTS  --> Request To Send
 *  8     5    CTS  <-- Clear To Send
 *  6     6    DSR  <-- Data Set Ready
 *  4    20    DTR  --> Data Terminal Ready
 *  1     8    DCD  <-- Data Carrier Detect
 *  9    22    RI   <-- Ring Indicator
 *  5     7    GND      Signal ground
 *
 * Note that it only makes sense to wait on handshake lines
 * activated from the receive side (DCE->DTE) here; in this
 * context "DCE" is the GPS. {CD,RI,CTS,DSR} is the
 * entire set of these.
 */
static const struct assoc hlines[] = {
    {TIOCM_CD, "TIOCM_CD"},
    {TIOCM_RI, "TIOCM_RI"},
    {TIOCM_DSR, "TIOCM_DSR"},
    {TIOCM_CTS, "TIOCM_CTS"},
};

static void usage(void)
{
        (void)fprintf(stderr, 
        "usage: ppscheck [OPTIONS] <device>\n\n"
#ifdef HAVE_GETOPT_LONG
        "  --help            Show this help, then exit.\n"
        "  --version         Show version, then exit.\n"
#endif
        "   -?               Show this help, then exit.\n"
        "   -h               Show this help, then exit.\n"
        "   -V               Show version, then exit.\n"
        "\n"
        "   <device>         Device to check (/dev/ttyS0, /dev/pps0, etc.).\n");
        exit(1);
}

int main(int argc, char *argv[])
{
    struct timespec ts;
    int fd;
    char ts_buf[TIMESPEC_LEN];
    const char *optstring = "?hV";
#ifdef HAVE_GETOPT_LONG
    int option_index = 0;
    static struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V' },
        {NULL, 0, NULL, 0},
    };
#endif

    while (1) {
        int ch;
#ifdef HAVE_GETOPT_LONG
        ch = getopt_long(argc, argv, optstring, long_options, &option_index);
#else
        ch = getopt(argc, argv, optstring);
#endif

        if (ch == -1) {
            break;
        }

        switch(ch){
        case '?':
        case 'h':
        default:
            usage();
            exit(0);
        case 'V':
            (void)printf("%s: %s\n", argv[0], REVISION);
            exit(EXIT_SUCCESS);
        }
    }
    argc -= optind;
    argv += optind;

    if (argc != 1)
            usage();

    fd = open(argv[0], O_RDONLY);

    if (fd == -1) {
        (void)fprintf(stderr,
                      "open(%s) failed: %d %.40s\n",
                      argv[0], errno, strerror(errno));
        exit(1);
    }

    (void)fprintf(stdout, "# Seconds  nanoSecs   Signals\n");
    for (;;) {
        if (ioctl(fd, TIOCMIWAIT, TIOCM_CD|TIOCM_DSR|TIOCM_RI|TIOCM_CTS) != 0) {
            (void)fprintf(stderr,
                          "PPS ioctl(TIOCMIWAIT) failed: %d %.40s\n",
                          errno, strerror(errno));
            break;
        } else {
            const struct assoc *sp;
            int handshakes;

            (void)clock_gettime(CLOCK_REALTIME, &ts);
            (void)ioctl(fd, TIOCMGET, &handshakes);
            (void)fprintf(stdout, "%s",
                          timespec_str(&ts, ts_buf, sizeof(ts_buf)));
            for (sp = hlines;
                 sp < hlines + sizeof(hlines)/sizeof(hlines[0]);
                 sp++)
                if ((handshakes & sp->mask) != 0) {
                    (void)fputc(' ', stdout);
                    (void)fputs(sp->string, stdout);
                }
            (void)fputc('\n', stdout);
        }
    }

    exit(EXIT_SUCCESS);
}

/* end */
// vim: set expandtab shiftwidth=4

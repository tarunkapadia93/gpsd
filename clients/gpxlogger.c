/*
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <math.h>
#ifdef HAVE_GETOPT_LONG
   #include <getopt.h>   // for getopt_long()
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>          // for atexit()
#include <string.h>
#include <time.h>
#include <unistd.h>          // for _exit()

#include "gps.h"
#include "gpsdclient.h"
#include "os_compat.h"
#include "timespec.h"

static char *progname;
static struct fixsource_t source;

/**************************************************************************
 *
 * Transport-layer-independent functions
 *
 **************************************************************************/

static struct gps_data_t gpsdata;
static FILE *logfile;
static bool intrack = false;
static time_t timeout = 5;      /* seconds */
static double minmove = 0;      /* meters */
static int debug;
static int sig_flag = 0;

static void print_gpx_header(void)
{
    char tbuf[CLIENT_DATE_MAX+1];

    (void)fprintf(logfile, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    (void)fprintf(logfile, "<gpx version=\"1.1\" creator=\"GPSD %s - %s\"\n",
                  VERSION, GPSD_URL);
    (void)fprintf(logfile,
         "        xmlns:xsi=\"https://www.w3.org/2001/XMLSchema-instance\"\n");
    (void)fprintf(logfile,
         "        xmlns=\"http://www.topografix.com/GPX/1/1\"\n");
    (void)fprintf(logfile
         ,"        xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1\n");
    (void)fprintf(logfile
         ,"        http://www.topografix.com/GPX/1/1/gpx.xsd\">\n");
    (void)fprintf(logfile, " <metadata>\n");
    (void)fprintf(logfile, "  <time>%s</time>\n",
         now_to_iso8601(tbuf, sizeof(tbuf)));
    (void)fprintf(logfile," </metadata>\n");
    (void)fflush(logfile);
}

static void print_gpx_trk_end(void)
{
    (void)fprintf(logfile,"  </trkseg>\n");
    (void)fprintf(logfile," </trk>\n");
    (void)fflush(logfile);
}

static void print_gpx_footer(void)
{
    if (intrack)
        print_gpx_trk_end();
    (void)fprintf(logfile,"</gpx>\n");
    (void)fclose(logfile);
}

static void print_gpx_trk_start(void)
{
    (void)fprintf(logfile," <trk>\n");
    (void)fprintf(logfile,"  <src>GPSD %s</src>\n", VERSION);
    (void)fprintf(logfile,"  <trkseg>\n");
    (void)fflush(logfile);
}

static void print_fix(struct gps_data_t *gpsdata, timespec_t ts_time)
{
    char tbuf[CLIENT_DATE_MAX+1];

    (void)fprintf(logfile,"   <trkpt lat=\"%.9f\" lon=\"%.9f\">\n",
                 gpsdata->fix.latitude, gpsdata->fix.longitude);

    /*
     * From the specification at https://www.topografix.com/GPX/1/1/gpx.xsd
     * the <ele> tag is defined as "Elevation (in meters) of the point."
     * This is ambiguous between HAE and orthometric height (above geoid,
     * aka MSL).
     * gpsd has historically used HAE and MSL randomly for altitude.
     * gpsd now explicitly supports distinct HAE and MSL.
     */
    if ((isfinite(gpsdata->fix.altHAE) != 0))
        (void)fprintf(logfile,"    <ele>%.4f</ele>\n", gpsdata->fix.altHAE);

    (void)fprintf(logfile,"    <time>%s</time>\n",
                 timespec_to_iso8601(ts_time, tbuf, sizeof(tbuf)));
    if (STATUS_DGPS == gpsdata->fix.status) {
        // FIXME: other status values?
        (void)fprintf(logfile,"    <fix>dgps</fix>\n");
    } else {
        switch (gpsdata->fix.mode) {
        case MODE_3D:
            (void)fprintf(logfile,"    <fix>3d</fix>\n");
            break;
        case MODE_2D:
            (void)fprintf(logfile,"    <fix>2d</fix>\n");
            break;
        case MODE_NO_FIX:
            (void)fprintf(logfile,"    <fix>none</fix>\n");
            break;
        default:
            /* don't print anything if no fix indicator */
            break;
        }
    }

    if ((gpsdata->fix.mode > MODE_NO_FIX) && (gpsdata->satellites_used > 0))
        (void)fprintf(logfile,"    <sat>%d</sat>\n", gpsdata->satellites_used);
    if (isfinite(gpsdata->dop.hdop) != 0)
        (void)fprintf(logfile,"    <hdop>%.1f</hdop>\n", gpsdata->dop.hdop);
    if (isfinite(gpsdata->dop.vdop) != 0)
        (void)fprintf(logfile,"    <vdop>%.1f</vdop>\n", gpsdata->dop.vdop);
    if (isfinite(gpsdata->dop.pdop) != 0)
        (void)fprintf(logfile,"    <pdop>%.1f</pdop>\n", gpsdata->dop.pdop);

    (void)fprintf(logfile,"   </trkpt>\n");
    (void)fflush(logfile);
}

// cleanup as an atexit() handler
static void cleanup(void)
{
        print_gpx_footer();
        (void)gps_close(&gpsdata);
        /* don't clutter the logs on Ctrl-C */
        if (0 != sig_flag && SIGINT != sig_flag) {
            syslog(LOG_INFO, "exiting, signal %d received", sig_flag);
        }
}

static void conditionally_log_fix(struct gps_data_t *gpsdata)
{
    static timespec_t ts_time, old_ts_time, ts_diff;
    static double old_lat, old_lon;
    static bool first = true;

    if (0 != sig_flag) {
        if (SIGINT != sig_flag) {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }

    ts_time = gpsdata->fix.time;
    if (TS_EQ(&ts_time, &old_ts_time) || gpsdata->fix.mode < MODE_2D)
        return;

    /* may not be worth logging if we've moved only a very short distance */
    if (0 < minmove && !first && earth_distance(
                                        gpsdata->fix.latitude,
                                        gpsdata->fix.longitude,
                                        old_lat, old_lon) < minmove)
        return;

    /*
     * Make new track if the jump in time is above
     * timeout.  Handle jumps both forward and
     * backwards in time.  The clock sometimes jumps
     * backward when gpsd is submitting junk on the
     * dbus.
     */
    TS_SUB(&ts_diff, &ts_time, &old_ts_time);
    if (labs((long)ts_diff.tv_sec) > timeout && !first) {
        print_gpx_trk_end();
        intrack = false;
    }

    if (!intrack) {
        print_gpx_trk_start();
        intrack = true;
        if (first)
            first = false;
    }

    old_ts_time = ts_time;
    if (0 < minmove) {
        old_lat = gpsdata->fix.latitude;
        old_lon = gpsdata->fix.longitude;
    }
    print_fix(gpsdata, ts_time);
}

static void quit_handler(int signum)
{
    // CWE-479: Signal Handler Use of a Non-reentrant Function
    // See: The C Standard, 7.14.1.1, paragraph 5 [ISO/IEC 9899:2011]
    // Can't log in a signal handler.  Can't even call exit().
    sig_flag = signum;
    return;
}

/**************************************************************************
 *
 * Main sequence
 *
 **************************************************************************/

static void usage(void)
{
    (void)fprintf(stderr,
         "Usage: %s [OPTIONS] [server[:port:[device]]]\n\n"
         "  -?                  Show this help, then exit\n"
#ifdef HAVE_GETOPT_LONG
         "  --daemonize         Daemonize\n"
         "  --debug LVL         Set debug level.\n"
         "  --export EXPORTMETHOD  Default %s\n"
         "  --exports           List available exports, then exit\n"
         "  --help              Show this help, then exit\n"
         "  --interval TIMEOUT  Create new track after TIMEOUT seconds. "
         "Default 5\n"
         "  --minmove MINMOVE   Minimum move in meters to log\n"
         "  --output FILNAME    Send output to file FILENAME\n"
         "  --reconnect         Retry when gpsd loses the fix.\n"
         "  --version           Show version, then exit\n"
#endif
         "  -D LVL              Set debug level.\n"
         "  -d                  Daemonize\n"
         "  -e EXPORTMETHOD     Default %s \n"
         "  -f FILENAME         Send output to file FILENAME\n"
         "  -h                  Show this help, then exit\n"
         "  -i TIMEOUT          Create new track after TIMEOUT seconds. "
         "Default 5\n"
         "  -l                  List available exports, then exit\n"
         "  -m MINMOVE          Minimum move in meters to log\n"
         "  -r                  Retry when gpsd loses the fix.\n"
         "  -V                  Show version and exit\n",
         progname,
#ifdef HAVE_GETOPT_LONG
         export_default()->name,
#endif
         export_default()->name);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    int ch;
    bool daemonize = false;
    bool reconnect = false;
    unsigned int flags = WATCH_ENABLE;
    struct exportmethod_t *method = NULL;
    const char *optstring = "?dD:e:f:hi:lm:rV";
#ifdef HAVE_GETOPT_LONG
    int option_index = 0;
    static struct option long_options[] = {
        {"daemonize", no_argument, NULL, 'd'},
        {"debug", required_argument, NULL, 'D'},
        {"export", required_argument, NULL, 'e'},
        {"exports", no_argument, NULL, 'l'},
        {"help", no_argument, NULL, 'h'},
        {"interval", required_argument, NULL, 'i'},
        {"minmove", required_argument, NULL, 'm'},
        {"output", required_argument, NULL, 'f'},
        {"reconnect", no_argument, NULL, 'r' },
        {"version", no_argument, NULL, 'V' },
        {NULL, 0, NULL, 0},
    };
#endif

    progname = argv[0];

    method = export_default();
    if (method == NULL) {
        (void)fprintf(stderr, "%s: no export methods.\n", progname);
        exit(EXIT_FAILURE);
    }

    logfile = stdout;
    while (1) {
#ifdef HAVE_GETOPT_LONG
        ch = getopt_long(argc, argv, optstring, long_options, &option_index);
#else
        ch = getopt(argc, argv, optstring);
#endif

        if (ch == -1) {
            break;
        }

        switch (ch) {
        case 'd':
            openlog(basename(progname), LOG_PID | LOG_PERROR, LOG_DAEMON);
            daemonize = true;
            break;
        case 'D':
            debug = atoi(optarg);
            gps_enable_debug(debug, logfile);
            break;
        case 'e':
            method = export_lookup(optarg);
            if (method == NULL) {
                (void)fprintf(stderr,
                              "%s: %s is not a known export method.\n",
                              progname, optarg);
                exit(EXIT_FAILURE);
            }
            break;
       case 'f':       /* Output file name. */
            {
                char   *fname = NULL;
                time_t  t;
                size_t  s = 0;
                size_t fnamesize = strlen(optarg) + 1;

                t = time(NULL);
                while (s == 0) {
                    char *newfname = realloc(fname, fnamesize);
                    if (newfname == NULL) {
                        syslog(LOG_ERR, "realloc failed.");
                        goto bailout;
                    } else {
                        fname = newfname;
                    }
                    s = strftime(fname, fnamesize-1, optarg, localtime(&t));
                    if (!s) {
                        /* expanded filename did not fit in string, try 
                         * a bigger string */
                        fnamesize += 1024;
                    }
                }
                fname[s] = '\0';;
                logfile = fopen(fname, "w");
                if (logfile == NULL) {
                    syslog(LOG_ERR,
                           "Failed to open %s: %s, logging to stdout.",
                           fname, strerror(errno));
                    logfile = stdout;
                }
            bailout:
                free(fname);
                break;
            }
        case 'i':               /* set polling interval */
            timeout = (time_t) atoi(optarg);
            if (timeout < 1)
                timeout = 1;
            if (timeout >= 3600)
                (void)fprintf(stderr,
                              "WARNING: track timeout is an hour or more!\n");
            break;
        case 'l':
            export_list(stderr);
            exit(EXIT_SUCCESS);
        case 'm':
            minmove = (double )atoi(optarg);
            break;
        case 'r':
            reconnect = true;
            break;
        case 'V':
            (void)fprintf(stderr, "%s: version %s (revision %s)\n",
                          progname, VERSION, REVISION);
            exit(EXIT_SUCCESS);
        case '?':
        case 'h':
        default:
            usage();
            /* NOTREACHED */
        }
    }

    if (daemonize && logfile == stdout) {
        syslog(LOG_ERR, "Daemon mode with no valid logfile name - exiting.");
        exit(EXIT_FAILURE);
    }

    if (method->magic != NULL) {
        source.server = (char *)method->magic;
        source.port = NULL;
        source.device = NULL;
    } else {
        source.server = (char *)"localhost";
        source.port = (char *)DEFAULT_GPSD_PORT;
        source.device = NULL;
    }

    if (optind < argc) {
        /* in this case, switch to the method "socket" always */
        gpsd_source_spec(argv[optind], &source);
    }
#if 0
    (void)fprintf(logfile,"<!-- server: %s port: %s  device: %s -->\n",
                 source.server, source.port, source.device);
#endif

    /* catch all interesting signals */
    (void)signal(SIGTERM, quit_handler);
    (void)signal(SIGQUIT, quit_handler);
    (void)signal(SIGINT, quit_handler);

    /* might be time to daemonize */
    if (daemonize) {
        /* not SuS/POSIX portable, but we have our own fallback version */
        if (os_daemon(0, 0) != 0)
            (void) fprintf(stderr,"daemonization failed: %s\n", strerror(errno));
    }

    //syslog (LOG_INFO, "---------- STARTED ----------");

    if (gps_open(source.server, source.port, &gpsdata) != 0) {
        (void)fprintf(stderr,
                      "%s: no gpsd running or network error: %d, %s\n",
                      progname, errno, gps_errstr(errno));
        exit(EXIT_FAILURE);
    }

    if (source.device != NULL)
        flags |= WATCH_DEVICE;
    if (NULL != source.port) {
        // only to sockets, not shared memory or dbus
        if (0 > gps_stream(&gpsdata, flags, source.device)) {
            syslog(LOG_ERR, "gps_stream() failed");
            exit(EXIT_FAILURE);
        }
    }

    print_gpx_header();
    // make sure footer added on exit
    if (0 != atexit(cleanup)) {
        syslog(LOG_ERR, "atexit() failed");
        exit(EXIT_FAILURE);
    }

    while (0 > gps_mainloop(&gpsdata, timeout * 1000000,
                            conditionally_log_fix)) {
        // fell out of mainloop, some sort of error, or just a timeout
        if (!reconnect || 0 != sig_flag) {
            // give up
            break;
        }
        // avoid banging on reconnect
        (void)sleep(timeout);
        syslog(LOG_INFO, "timeout; about to reconnect");
    }

    if (0 != sig_flag && SIGINT != sig_flag) {
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}

// vim: set expandtab shiftwidth=4

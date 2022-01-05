/*
 * gpsdclient.c -- support functions for GPSD clients
 *
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* for strcasecmp() */

#include "gps.h"
#include "gpsdclient.h"
#include "os_compat.h"

static struct exportmethod_t exportmethods[] = {
#if defined(DBUS_EXPORT_ENABLE)
    {"dbus", GPSD_DBUS_EXPORT, "DBUS broadcast"},
#endif /* defined(DBUS_EXPORT_ENABLE) */
#ifdef SHM_EXPORT_ENABLE
    {"shm", GPSD_SHARED_MEMORY, "shared memory"},
#endif /* SOCKET_EXPORT_ENABLE */
#ifdef SOCKET_EXPORT_ENABLE
    {"sockets", NULL, "JSON via sockets"},
#endif /* SOCKET_EXPORT_ENABLE */
};

/* convert value of double degrees to a buffer.
 * add suffix_pos or suffix_neg depending on sign.
 * buffer should be at least 20 bytes.
 * Return a pointer to the buffer.
 *
 * deg_str_type:
 *      deg_dd     : return DD.ddddddd[suffix]
 *      deg_ddmm   : return DD MM.mmmmmm'[suffix]
 *      deg_ddmmss : return DD MM' SS.sssss"[suffix]
 *
 * returns 'n/a' for 360 < f or -360 > f
 *
 * NOTE: 360.0 is rolled over to 0.0
 *
 * for cm level accuracy, at sea level, we need degrees
 * to 7+ decimal places
 * Ref: https://en.wikipedia.org/wiki/Decimal_degrees
 *
 */
char *deg_to_str2(enum deg_str_type type, double f,
                  char *buf, unsigned int buf_size,
                  const char *suffix_pos, const char *suffix_neg)

{
    int dsec, sec, deg, min;
    double fdsec, fsec, fdeg, fmin;
    const char *suffix = "";

    if (20 > buf_size) {
        (void)strlcpy(buf, "Err", buf_size);
        return buf;
    }

    if (!isfinite(f) || 360.0 < fabs(f)) {
        (void)strlcpy(buf, "n/a", buf_size);
        return buf;
    }

    /* suffix? */
    if (0.0 > f) {
        f = -f;
        if (NULL != suffix_neg) {
            suffix = suffix_neg;
        }
    } else if (NULL != suffix_pos) {
        suffix = suffix_pos;
    }

    /* add rounding quanta */
    /* IEEE 754 wants round to nearest even.
     * We cheat and just round to nearest.
     * Intel trying to kill off round to nearest even. */
    switch (type) {
    default:
        /* huh? */
        type = deg_dd;
        /* It's not worth battling fallthrough warnings just for two lines */
        f += 0.5 * 1e-8;              /* round up */
        break;
    case deg_dd:
        /* DD.dddddddd */
        f += 0.5 * 1e-8;              /* round up */
        break;
    case deg_ddmm:
        /* DD MM.mmmmmm */
        f += (0.5 * 1e-6) / 60;       /* round up */
        break;
    case deg_ddmmss:
        f += (0.5 * 1e-5) / 3600;     /* round up */
        break;
    }
    fmin = modf(f, &fdeg);
    deg = (int)fdeg;
    if (360 == deg) {
        /* fix round-up roll-over */
        deg = 0;
        fmin = 0.0;
    }

    if (deg_dd == type) {
        /* DD.dddddddd */
        long frac_deg = (long)(fmin * 100000000.0);
        /* cm level accuracy requires the %08ld */
        (void)snprintf(buf, buf_size, "%3d.%08ld%s", deg, frac_deg, suffix);
        return buf;
    }

    fsec = modf(fmin * 60, &fmin);
    min = (int)fmin;

    if (deg_ddmm == type) {
        /* DD MM.mmmmmm */
        sec = (int)(fsec * 1000000.0);
        (void)snprintf(buf, buf_size, "%3d %02d.%06d'%s", deg, min, sec,
                       suffix);
        return buf;
    }
    /* else DD MM SS.sss */
    fdsec = modf(fsec * 60.0, &fsec);
    sec = (int)fsec;
    dsec = (int)(fdsec * 100000.0);
    (void)snprintf(buf, buf_size, "%3d %02d' %02d.%05d\"%s", deg, min, sec,
                   dsec, suffix);

    return buf;
}

/* convert absolute value of double degrees to a static string.
 * Return a pointer to the static string.
 * WARNING: Not thread safe.
 *
 * deg_str_type:
 *      deg_dd     : return DD.ddddddd
 *      deg_ddmm   : return DD MM.mmmmmm'
 *      deg_ddmmss : return DD MM' SS.sssss"
 *
 * returns 'n/a' for 360 < f
 *
 * NOTE: 360.0 is rolled over to 0.0
 *
 * for cm level accuracy, at sea level, we need degrees
 * to 7+ decimal places
 * Ref: https://en.wikipedia.org/wiki/Decimal_degrees
 *
 */
char *deg_to_str(enum deg_str_type type, double f)
{
    static char buf[20];

    return deg_to_str2(type, f, buf, sizeof(buf), "", "");
}

/*
 * check the environment to determine proper GPS units
 *
 * clients should only call this if no user preference is specified on
 * the command line or via X resources.
 *
 * return imperial    - Use miles/feet
 *        nautical    - Use knots/feet
 *        metric      - Use km/meters
 *        unspecified - use compiled default
 *
 * In order check these environment vars:
 *    GPSD_UNITS one of:
 *              imperial   = miles/feet
 *              nautical   = knots/feet
 *              metric     = km/meters
 *    LC_MEASUREMENT
 *              en_US      = miles/feet
 *              C          = miles/feet
 *              POSIX      = miles/feet
 *              [other]    = km/meters
 *    LANG
 *              en_US      = miles/feet
 *              C          = miles/feet
 *              POSIX      = miles/feet
 *              [other]    = km/meters
 *
 * if none found then return compiled in default
 */
enum unit gpsd_units(void)
{
    char *envu = NULL;

    if ((envu = getenv("GPSD_UNITS")) != NULL && *envu != '\0') {
        if (0 == strcasecmp(envu, "imperial")) {
            return imperial;
        }
        if (0 == strcasecmp(envu, "nautical")) {
            return nautical;
        }
        if (0 == strcasecmp(envu, "metric")) {
            return metric;
        }
        /* unrecognized, ignore it */
    }
    if (((envu = getenv("LC_MEASUREMENT")) != NULL && *envu != '\0')
        || ((envu = getenv("LANG")) != NULL && *envu != '\0')) {
        if (strncasecmp(envu, "en_US", 5) == 0
            || strcasecmp(envu, "C") == 0 || strcasecmp(envu, "POSIX") == 0) {
            return imperial;
        }
        /* Other, must be metric */
        return metric;
    }
    /* TODO: allow a compile time default here */
    return unspecified;
}

/* standard parsing of a GPS data source spec */
void gpsd_source_spec(const char *arg, struct fixsource_t *source)
{
    /* the casts attempt to head off a -Wwrite-strings warning */
    source->server = (char *)"localhost";
    source->port = (char *)DEFAULT_GPSD_PORT;
    source->device = NULL;

    if (arg != NULL) {
        char *colon1, *skipto, *rbrk;
        source->spec = (char *)arg;
        assert(source->spec != NULL);

        skipto = source->spec;
        if (*skipto == '[' && (rbrk = strchr(skipto, ']')) != NULL) {
            skipto = rbrk;
        }
        colon1 = strchr(skipto, ':');

        if (colon1 != NULL) {
            char *colon2;
            *colon1 = '\0';
            if (colon1 != source->spec) {
                source->server = source->spec;
            }
            if ('\0' != colon1[1] &&
                ':' != colon1[1]) {
                // override default only if there is a port string.
                source->port = colon1 + 1;
            }
            colon2 = strchr(colon1 + 1, ':');
            if (colon2 != NULL) {
                *colon2 = '\0';
                if ('\0' != colon2[1]) {
                    // override default only if there is a device string.
                    source->device = colon2 + 1;
                }
            }
        } else if (strchr(source->spec, '/') != NULL) {
            source->device = source->spec;
        } else {
            source->server = source->spec;
        }
    }

    if (*source->server == '[') {
        char *rbrk = strchr(source->server, ']');
        ++source->server;
        if (rbrk != NULL)
            *rbrk = '\0';
    }
}


/* lat/lon to Maidenhead */
char *maidenhead(double lat, double lon)
{
    /*
     * Specification at
     * https://en.wikipedia.org/wiki/Maidenhead_Locator_System
     *
     * There's a fair amount of slop in how Maidenhead converters operate
     * that can make it look like this one is wrong.
     *
     * 1. Many return caps for paces 5 and 6 when according to the spec
     *    they should return smalls.
     *
     * 2. Some converters, including QGrid from which this code was originally
     *    derived, add an 0.5 offset to the divided e and n just before it
     *    is cast to integer and used for places 5 and 6. This appears to be
     *    intended as a round-to-nearest hack (as opposed to the implicit
     *    round down from the cast). If I'm reading the spec right it
     *    is not correct to do this.
     */
    /* FIXME: convert lat/lon to integer seconds, then do all the
     * math on integers */
    static char buf[9];

    int t1;

    /* longitude */
    if (179.99999 < lon) {
        /* force 180, just inside lon_sq 'R'
         * otherwise we get illegal 'S' */
        lon = 179.99999;
    }

    lon += 180.0;
    // divide into 18 zones (fields) each 20 degrees lon
    t1 = (int)(lon / 20);
    buf[0] = (char)t1 + 'A';
    if ('R' < buf[0]) {
        /* A to R */
        buf[0] = 'R';
    }
    lon -= (float)t1 * 20.0;

    // divide into 10 zones (squares) each 2 degrees lon
    t1 = (int)lon / 2;
    buf[2] = (char)t1 + '0';
    lon -= (float)t1 * 2;

    // lon is now degrees
    lon *= 60.0;

    // divide into 24 zones (subsquares) each 5 minute (5/60 deg) lon
    t1 = (int)(lon / 5);
    buf[4] = (char) ((char)t1 + 'a');
    lon -= (float)(t1 * 5);

    // lon is now seconds
    lon *= 60.0;

    // divide into 10 zones (extended squares) each 30 seconds (5/600 deg) lon
    t1 = (int)(lon / 30);
    if (9 < t1) {
        // ugh, floating point gunk.
        t1 = 9;
    }
    buf[6] = (char) ((char)t1 + '0');

    /* latitude */
    if (89.99999 < lat) {
        /* force 90 to just inside lat_sq 'R'
         * otherwise we get illegal 'S' */
        lat = 89.99999;
    }

    lat += 90.0;
    // divide into 18 zones (fields) each 10 degrees lat
    t1 = (int)(lat / 10.0);
    buf[1] = (char)t1 + 'A';
    if ('R' < buf[1]) {
        /* A to R, North Pole is R */
        buf[1] = 'R';
    }
    lat -= (float)t1 * 10.0;

    // divide into 10 zones (squares) each 1 degrees lat
    buf[3] = (char)lat + '0';
    lat -= (int)lat;

    // lat is now degrees
    lat *= 60.0;

    // divide into 24 zones (subsquares) each 2.5 minute (5/120 deg) lat
    t1 = (int)(lat / 2.5);
    buf[5] = (char)((char)t1 + 'a');
    lat -= (float)(t1 * 2.5);

    // lat is now seconds
    lat *= 60.0;

    // divide into 10 zones (extended squares) each 15 seconds (5/1200 deg) lat
    t1 = (int)(lat / 15);
    if (9 < t1) {
        // ugh, floating point gunk.
        t1 = 9;
    }
    buf[7] = (char) ((char)t1 + '0');

    buf[8] = '\0';

    return buf;
}

#define NITEMS(x) (int)(sizeof(x)/sizeof(x[0])) /* from gpsd.h-tail */

/* Look up an available export method by name */
struct exportmethod_t *export_lookup(const char *name)
{
    struct exportmethod_t *mp, *method = NULL;

    for (mp = exportmethods;
         mp < exportmethods + NITEMS(exportmethods);
         mp++)
        if (strcmp(mp->name, name) == 0)
            method = mp;
    return method;
}

/* list known export methods */
void export_list(FILE *fp)
{
    struct exportmethod_t *method;

    for (method = exportmethods;
         method < exportmethods + NITEMS(exportmethods);
         method++)
        (void)fprintf(fp, "%s: %s\n", method->name, method->description);
}

struct exportmethod_t *export_default(void)
{
    return (NITEMS(exportmethods) > 0) ? &exportmethods[0] : NULL;
}

/* gpsdclient.c ends here */
// vim: set expandtab shiftwidth=4

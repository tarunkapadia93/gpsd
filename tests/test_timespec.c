/*
 * Unit test for timespec's
 * Also for parse_uri_dest(), and ntrip_parse_url
 *
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 */
/* first so the #defs work */
#include "gpsd_config.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>   /* required by C99, for int32_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compiler.h"       // for FALLTHROUGH
#include "gpsd.h"

#define TS_ZERO         {0,0}
#define TS_ZERO_ONE     {0,1}
#define TS_ZERO_ONEM    {0,1000000}
#define TS_ZERO_TWO     {0,2}
#define TS_ZERO_TWOM    {0,2000000}
#define TS_ZERO_TREES   {0,333333333}
#define TS_ZERO_SIXS7   {0,666666667}
#define TS_ZERO_NINES   {0,999999999}
#define TS_ONE          {1,0}
#define TS_ONE_ONE      {1,1}
#define TS_TWO          {2,0}
#define TS_N_ZERO_ONE   {0,-1}
#define TS_N_ZERO_TWO   {0,-2}
#define TS_N_ZERO_TREES {0,-333333333}
#define TS_N_ZERO_NINES {0,-999999999}
#define TS_N_ONE        {-1,0}

/* minutes, hours, days */
#define TS_ONEM         {60,0}                  /* one minute */
#define TS_ONEM_TREES   {60,333333333}          /* one minute, threes */
#define TS_ONEM_NINES   {60,999999999}          /* one minute, nines */
#define TS_ONEH         {3600,0}                /* one hour */
#define TS_ONEH_TREES   {3600,333333333}        /* one hour, threes */
#define TS_ONEH_NINES   {3600,999999999}        /* one hour, nines */
#define TS_ONED         {86400,0}               /* one day */
#define TS_ONED_TREES   {86400,333333333}       /* one day, threes */
#define TS_ONED_NINES   {86400,999999999}       /* one day, nines */
#define TS_N_ONEM       {-60,0}                 /* negative one minute */
#define TS_N_ONEH       {-3600,0}               /* negative one hour */
#define TS_N_ONED       {-86400,0}              /* negative one day */

/* Dec 31, 23:59 2037 GMT */
#define TS_2037         {2145916799, 0}
#define TS_2037_ONE     {2145916799, 1}
#define TS_2037_TWO     {2145916799, 2}
#define TS_2037_X       {2145916799, 123456789}
#define TS_2037_TREES   {2145916799, 333333333}
#define TS_2037_SIXS7   {2145916799, 666666667}
#define TS_2037_NINES   {2145916799, 999999999}
#define TS_N_2037_TREES {-2145916799, -333333333}
#define TS_N_2037_NINES {-2145916799, -999999999}

/* a 32 bit copy of timespec_diff_ns() to force a 32 bit int */
/* used to demonstrate how 32 bit longs can not work */
#define timespec_diff_ns32(x, y) \
 (int32_t)((int32_t)(((x).tv_sec-(y).tv_sec)*NS_IN_SEC)+(x).tv_nsec-(y).tv_nsec)

/* a 64 bit copy of timespec_diff_ns() to force a 64 bit int */
/* used to demonstrate how 64 bit long longs can work */
#define timespec_diff_ns64(x, y) \
 (int64_t)((int64_t)(((x).tv_sec-(y).tv_sec)*NS_IN_SEC)+(x).tv_nsec-(y).tv_nsec)

/* convert long long ns to a timespec */
#define ns_to_timespec(ts, ns) \
        (ts).tv_sec  = ns / NS_IN_SEC; \
        (ts).tv_nsec = ns % NS_IN_SEC;

/* convert double to a timespec */
static inline void d_str( const double d, char *buf, size_t buf_size)
{
    /* convert to string */
    if ( 0 <= d ) {
        (void) snprintf( buf, buf_size, " %.9f", d);
    } else {
        (void) snprintf( buf, buf_size, "%.9f", d);
    }
}

/* a - b should be c */
struct subtract_test {
        struct timespec a;
        struct timespec b;
        struct timespec c;
        bool last;              /* last test marker */
};

struct subtract_test subtract_tests[] = {
        { TS_ZERO,        TS_ZERO,        TS_ZERO,         0},
        { TS_ONE,         TS_ONE,         TS_ZERO,         0},
        { TS_ZERO_ONE,    TS_ZERO_ONE,    TS_ZERO,         0},
        { TS_ONE_ONE,     TS_ONE_ONE,     TS_ZERO,         0},
        { TS_N_ONE,       TS_N_ONE,       TS_ZERO,         0},
        { TS_N_ZERO_ONE,  TS_N_ZERO_ONE,  TS_ZERO,         0},
        { TS_ZERO_TREES,  TS_ZERO_TREES,  TS_ZERO,         0},
        { TS_ZERO_NINES,  TS_ZERO_NINES,  TS_ZERO,         0},
        { TS_ZERO_TREES,  TS_ZERO,        TS_ZERO_TREES,   0},
        { TS_ZERO,        TS_N_ONE,       TS_ONE,          0},
        { TS_ONE,         TS_ZERO,        TS_ONE,          0},
        { TS_TWO,         TS_ONE,         TS_ONE,          0},
        { TS_ONE_ONE,     TS_ONE,         TS_ZERO_ONE,     0},
        { TS_ONE,         TS_ZERO_TREES,  TS_ZERO_SIXS7,   0},
        { TS_ONE,         TS_ZERO_NINES,  TS_ZERO_ONE,     0},
        { TS_ZERO_TWO,    TS_ZERO_ONE,    TS_ZERO_ONE,     0},
        { TS_2037_ONE,    TS_2037,        TS_ZERO_ONE,     0},
        { TS_ONE_ONE,     TS_ZERO_NINES,  TS_ZERO_TWO,     0},
        { TS_ONEM,        TS_ZERO,        TS_ONEM,         0},
        { TS_ONEM_TREES,  TS_ZERO,        TS_ONEM_TREES,   0},
        { TS_ONEM_NINES,  TS_ZERO,        TS_ONEM_NINES,   0},
        { TS_ZERO,        TS_ONEM,        TS_N_ONEM,       0},
        { TS_ONEH,        TS_ZERO,        TS_ONEH,         0},
        { TS_ONEH_TREES,  TS_ZERO,        TS_ONEH_TREES,   0},
        { TS_ONEH_NINES,  TS_ZERO,        TS_ONEH_NINES,   0},
        { TS_ZERO,        TS_ONEH,        TS_N_ONEH,       0},
        { TS_ONED,        TS_ZERO,        TS_ONED,         0},
        { TS_ONED_TREES,  TS_ZERO,        TS_ONED_TREES,   0},
        { TS_ONED_NINES,  TS_ZERO,        TS_ONED_NINES,   0},
        { TS_ZERO,        TS_ONED,        TS_N_ONED,       0},
        { TS_2037_NINES,  TS_2037,        TS_ZERO_NINES,   0},
        { TS_2037_TREES,  TS_ZERO,        TS_2037_TREES,   0},
        { TS_2037_SIXS7,  TS_2037,        TS_ZERO_SIXS7,   0},
        { TS_2037_TREES,  TS_2037,        TS_ZERO_TREES,   0},
        { TS_2037_NINES,  TS_ZERO,        TS_2037_NINES,   0},
        { TS_ZERO,        TS_ONE,         TS_N_ONE,        0},
        { TS_ONE,         TS_TWO,         TS_N_ONE,        0},
        { TS_ZERO,        TS_ZERO_ONE,    TS_N_ZERO_ONE,   0},
        { TS_ONE,         TS_ONE_ONE,     TS_N_ZERO_ONE,   0},
        { TS_ZERO_ONE,    TS_ZERO_TWO,    TS_N_ZERO_ONE,   0},
        { TS_2037,        TS_2037_ONE,    TS_N_ZERO_ONE,   0},
        { TS_ZERO_NINES,  TS_ONE_ONE,     TS_N_ZERO_TWO,   0},
        { TS_2037,        TS_2037_NINES,  TS_N_ZERO_NINES, 0},
        { TS_ZERO,        TS_2037_NINES,  TS_N_2037_NINES, 1},
};

typedef struct ts_to_ms_test {
        struct timespec input;
        int64_t expected;
        bool last;
} ts_to_ms_test_t;

struct ts_to_ms_test ts_to_ms_tests[] = {
        { TS_ZERO,          0, 0},
        { TS_ZERO_ONE,      0, 0},
        { TS_ZERO_ONEM,     1, 0},
        { TS_ZERO_TWO,      0, 0},
        { TS_ZERO_TWOM,     2, 0},
        { TS_ZERO_NINES,    999, 0},
        { TS_ONE,           1000, 0},
        { TS_ONE_ONE,       1000, 0},
        { TS_TWO,           2000, 0},
        { TS_N_ZERO_ONE,    0, 0},
        { TS_N_ZERO_TWO,    0, 0},
        { TS_N_ZERO_NINES,  -999, 0},
        { TS_N_ONE,         -1000, 0},
        { TS_ONEM,          60000, 0},
        { TS_ONEM_TREES,    60333, 0},
        { TS_ONEH,          3600000, 0},
        { TS_ONEH_TREES,    3600333, 0},
        { TS_ONED,          86400000, 0},
        { TS_ONED_TREES,    86400333, 0},
        { TS_N_ONEM,        -60000, 0},
        { TS_N_ONEH,        -3600000, 0},
        { TS_N_ONED,        -86400000, 0},
        { { -1, NS_IN_MS},  -999, 0},
        { { -1, -NS_IN_MS}, -1001, 0},
        // Note no (extra) loss of precision on the following
        { TS_2037,          2145916799000ULL, 0},
        { TS_2037_ONE,      2145916799000ULL, 0},
        { TS_2037_TREES,    2145916799333ULL, 0},
        { TS_2037_NINES,    2145916799999ULL, 1},
};

/*
 * test timespec_t to int64_t of milli seconds: TSTOMS()
 *
 */
static int test_ts_to_ms(int verbose)
{
    struct ts_to_ms_test *p = ts_to_ms_tests;
    int fail_count = 0;

    while (1) {
        char buf_i[TIMESPEC_LEN];
        int64_t result;

        result = TSTOMS(&p->input);
        timespec_str(&p->input, buf_i, sizeof(buf_i));
        if (p->expected != result) {
                printf("%21s = %lld, FAIL s/b %lld\n",
                       buf_i, (long long) result, (long long) p->expected);
                fail_count++;
        } else if ( verbose ) {
                printf("%21s = %lld\n", buf_i, (long long) result);
        }

        if (p->last) {
                break;
        }
        p++;
    };

    if (fail_count) {
        printf("timespec subtract test failed %d tests\n", fail_count );
    } else {
        puts("timespec subtract test succeeded\n");
    }
    return fail_count;
}

/*
 * test subtractions using native timespec math: TS_SUB()
 *
 */
static int test_ts_subtract(int verbose)
{
    struct subtract_test *p = subtract_tests;
    int fail_count = 0;

    while (1) {
        char buf_a[TIMESPEC_LEN];
        char buf_b[TIMESPEC_LEN];
        char buf_c[TIMESPEC_LEN];
        char buf_r[TIMESPEC_LEN];
        struct timespec r;

        TS_SUB(&r, &p->a, &p->b);
        timespec_str( &p->a, buf_a, sizeof(buf_a) );
        timespec_str( &p->b, buf_b, sizeof(buf_b) );
        timespec_str( &p->c, buf_c, sizeof(buf_c) );
        timespec_str( &r,    buf_r, sizeof(buf_r) );
        if ((p->c.tv_sec != r.tv_sec) || (p->c.tv_nsec != r.tv_nsec)) {
                printf("%21s - %21s = %21s, FAIL s/b %21s\n",
                       buf_a, buf_b, buf_r, buf_c);
                fail_count++;
        } else if ( verbose ) {
                printf("%21s - %21s = %21s\n", buf_a, buf_b, buf_r);
        }

        if (p->last) {
                break;
        }
        p++;
    };

    if (fail_count) {
        printf("timespec subtract test failed %d tests\n", fail_count );
    } else {
        puts("timespec subtract test succeeded\n");
    }
    return fail_count;
}

/*
 * test subtractions using timespec_diff_ns()
 *
 */
static int test_ns_subtract( int verbose )
{
    struct subtract_test *p = subtract_tests;
    int fail_count = 0;

    while ( 1 ) {
        char buf_a[TIMESPEC_LEN];
        char buf_b[TIMESPEC_LEN];
        char buf_c[TIMESPEC_LEN];
        char buf_r[TIMESPEC_LEN];
        struct timespec r;
        long long r_ns;

        r_ns = timespec_diff_ns(p->a, p->b);
        timespec_str( &p->a, buf_a, sizeof(buf_a) );
        timespec_str( &p->b, buf_b, sizeof(buf_b) );
        timespec_str( &p->c, buf_c, sizeof(buf_c) );
        ns_to_timespec( r, r_ns);
        timespec_str( &r,    buf_r, sizeof(buf_r) );
        if ( (p->c.tv_sec != r.tv_sec) || (p->c.tv_nsec != r.tv_nsec) ) {
                printf("%21s - %21s = %21s, FAIL s/b %21s\n",
                        buf_a, buf_b, buf_r, buf_c);
                fail_count++;
        } else if ( verbose ) {
                printf("%21s - %21s = %21s\n", buf_a, buf_b, buf_r);
        }

        if ( p->last ) {
                break;
        }
        p++;
    };

    if ( fail_count ) {
        printf("ns subtract test failed %d tests\n", fail_count );
    } else {
        puts("ns subtract test succeeded\n");
    }
    return fail_count;
}

typedef struct format_test {
        struct timespec input;
        char *expected;
        bool last;
} format_test_t;

struct format_test format_tests[] = {
        { TS_ZERO,         " 0.000000000", 0},
        { TS_ZERO_ONE,     " 0.000000001", 0},
        { TS_ZERO_TWO,     " 0.000000002", 0},
        { TS_ZERO_NINES,   " 0.999999999", 0},
        { TS_ONE,          " 1.000000000", 0},
        { TS_ONE_ONE,      " 1.000000001", 0},
        { TS_TWO,          " 2.000000000", 0},
        { TS_N_ZERO_ONE,   "-0.000000001", 0},
        { TS_N_ZERO_TWO,   "-0.000000002", 0},
        { TS_N_ZERO_NINES, "-0.999999999", 0},
        { TS_N_ONE,        "-1.000000000", 0},
        { TS_ONEM,         " 60.000000000", 0},
        { TS_ONEM_TREES,   " 60.333333333", 0},
        { TS_ONEH,         " 3600.000000000", 0},
        { TS_ONEH_TREES,   " 3600.333333333", 0},
        { TS_ONED,         " 86400.000000000", 0},
        { TS_ONED_TREES,   " 86400.333333333", 0},
        { TS_N_ONEM,        "-60.000000000", 0},
        { TS_N_ONEH,        "-3600.000000000", 0},
        { TS_N_ONED,        "-86400.000000000", 0},
        { { -1, 1},        "-1.000000001", 0},
        { { -1, -1},       "-1.000000001", 0},
        { TS_2037,         " 2145916799.000000000", 0},
        { TS_2037_ONE,     " 2145916799.000000001", 0},
        { TS_2037_TREES,   " 2145916799.333333333", 0},
        { TS_2037_NINES,   " 2145916799.999999999", 1},
};

static int test_format(int verbose )
{
    format_test_t *p = format_tests;
    int fail_count = 0;

    while ( 1 ) {
        char buf[TIMESPEC_LEN];
        int fail;

        timespec_str( &p->input, buf, sizeof(buf) );
        fail = strncmp( buf, p->expected, TIMESPEC_LEN);
        if ( fail ) {
                printf("%21s, FAIL s/b: %21s\n", buf,  p->expected);
                fail_count++;
        } else if ( verbose )  {
                printf("%21s\n", buf);
        }

        if ( p->last ) {
                break;
        }
        p++;
    };

    if ( fail_count ) {
        printf("timespec_str test failed %d tests\n", fail_count );
    } else {
        puts("timespec_str test succeeded\n");
    }
    return fail_count;
}

typedef struct {
    unsigned short week;
    int leap_seconds;
    timespec_t ts_tow;
    timespec_t ts_exp;  // expected result
    char *exp_s;        // expected string
    bool last;
} gpstime_test_t;

gpstime_test_t gpstime_tests[] = {
    // GPS time zero
    {0, 0, TS_ZERO, {315964800, 000000000}, "1980-01-06T00:00:00.000Z", 0},
    // GPS first roll over
    {1024, 7, TS_ZERO, {935279993, 000000000}, "1999-08-21T23:59:53.000Z", 0},
    // GPS first roll over
    {2048, 18, TS_ZERO, {1554595182, 000000000}, "2019-04-06T23:59:42.000Z", 0},
    {2076, 18, {239910, 100000000}, {1571769492, 100000000},
     "2019-10-22T18:38:12.100Z", 1},
};

static int test_gpsd_gpstime_resolv(int verbose)
{

    char res_s[128];
    char buf[20];
    int fail_count = 0;
    struct gps_device_t session;
    struct gps_context_t context;
    timespec_t ts_res;
    gpstime_test_t *p = gpstime_tests;

    memset(&session, 0, sizeof(session));
    memset(&context, 0, sizeof(context));
    session.context = &context;
    context.errout.debug = 0;          // a handle to change debug level

    while ( 1 ) {

        /* setup preconditions */
        context.gps_week = p->week;
        context.leap_seconds = p->leap_seconds;
        ts_res = gpsd_gpstime_resolv(&session, p->week, p->ts_tow);
        (void)timespec_to_iso8601(ts_res, res_s, sizeof(res_s));

        if (p->ts_exp.tv_sec != ts_res.tv_sec ||
            p->ts_exp.tv_nsec != ts_res.tv_nsec ||
            strcmp(res_s, p->exp_s) ) {
                // long long for 32-bit OS
                printf("FAIL %s s/b: %s\n"
                       "     %s s/b %s\n",
                       timespec_str(&ts_res, buf, sizeof(buf)),
                       timespec_str(&p->ts_exp, buf, sizeof(buf)),
                       res_s, p->exp_s);
                fail_count++;
        } else if ( verbose )  {
                printf("%s (%s)\n",
                       timespec_str(&p->ts_exp, buf, sizeof(buf)),
                       p->exp_s);
        }
        if ( p->last ) {
                break;
        }
        p++;
    }

    if ( fail_count ) {
        printf("test_gpsd_gpstime_resolv test failed %d tests\n", fail_count);
    } else {
        puts("test_gpsd_gpstime_resolv test succeeded\n");
    }
    return fail_count;
}

static int ex_subtract_float(void)
{
    struct subtract_test *p = subtract_tests;
    int fail_count = 0;

    printf( "\n\nsubtract test examples using doubles,floats,longs:\n"
            " ts:  TS_SUB()\n"
            " l:   timespec_to_ns() math\n"
            " l32: timespec_to_ns() math with 32 bit long\n"
            " l64: timespec_to_ns() math with 64 bit long\n"
            " f:   float math\n"
            " d:   double float math\n"
            "\n");

    while ( 1 ) {
        char buf_a[TIMESPEC_LEN];
        char buf_b[TIMESPEC_LEN];
        char buf_c[TIMESPEC_LEN];
        char buf_r[TIMESPEC_LEN];
        char buf_l[TIMESPEC_LEN];
        char buf_l32[TIMESPEC_LEN];
        char buf_l64[TIMESPEC_LEN];
        char buf_f[TIMESPEC_LEN];
        char buf_d[TIMESPEC_LEN];
        struct timespec ts_r;
        struct timespec ts_l;
        struct timespec ts_l32;
        struct timespec ts_l64;
        float f_a, f_b, f_r;
        double d_a, d_b, d_r;
        long long l;
        int32_t l32;  /* simulate a 32 bit long */
        int64_t l64;  /* simulate a 64 bit long */
        const char *fail_ts = "";
        const char *fail_l = "";
        const char *fail_l32 = "";
        const char *fail_l64 = "";
        const char *fail_f = "";
        const char *fail_d = "";

        /* timespec math */
        TS_SUB(&ts_r, &p->a, &p->b);

        /* float math */
        f_a = TSTONS( &p->a );
        f_b = TSTONS( &p->b );
        f_r = f_a - f_b;

        /* double float math */
        d_a = TSTONS( &p->a );
        d_b = TSTONS( &p->b );
        d_r = d_a - d_b;

        /* long math */
        l = timespec_diff_ns( p->a, p->b);
        l32 = timespec_diff_ns32( p->a, p->b);
        l64 = timespec_diff_ns64( p->a, p->b);

        /* now convert to strings */
        timespec_str( &p->a, buf_a, sizeof(buf_a) );
        timespec_str( &p->b, buf_b, sizeof(buf_b) );
        timespec_str( &p->c, buf_c, sizeof(buf_c) );
        timespec_str( &ts_r, buf_r, sizeof(buf_r) );

        ns_to_timespec( ts_l, l );
        timespec_str( &ts_l, buf_l, sizeof(buf_l) );
        ns_to_timespec( ts_l32, l32 );
        timespec_str( &ts_l32, buf_l32, sizeof(buf_l32) );
        ns_to_timespec( ts_l64, l64);
        timespec_str( &ts_l64, buf_l64, sizeof(buf_l64) );
        d_str( f_r, buf_f, sizeof(buf_f) );
        d_str( d_r, buf_d, sizeof(buf_d) );

        /* test strings */
        if ( strcmp( buf_r, buf_c) ) {
            fail_ts = "FAIL";
            fail_count++;
        }
        if ( strcmp( buf_l, buf_c) ) {
            fail_l = "FAIL";
            fail_count++;
        }
        if ( strcmp( buf_l32, buf_c) ) {
            fail_l32 = "FAIL";
            fail_count++;
        }
        if ( strcmp( buf_l64, buf_c) ) {
            fail_l64 = "FAIL";
            fail_count++;
        }
        if ( strcmp( buf_f, buf_c) ) {
            fail_f = "FAIL";
            fail_count++;
        }
        if ( strcmp( buf_d, buf_c) ) {
            fail_d = "FAIL";
            fail_count++;
        }
        printf("ts:  %21s - %21s = %21s %s\n"
               "l;   %21s - %21s = %21lld %s\n"
               "l32; %21s - %21s = %21lld %s\n"
               "l64; %21s - %21s = %21lld %s\n"
               "f;   %21.9f - %21.9f = %21.9f %s\n"
               "d;   %21.9f - %21.9f = %21.9f %s\n"
               "\n",
                buf_a, buf_b, buf_r, fail_ts,
                buf_a, buf_b, l, fail_l,
                buf_a, buf_b, (long long)l32, fail_l32,
                buf_a, buf_b, (long long)l64, fail_l64,
                f_a, f_b, f_r, fail_f,
                d_a, d_b, d_r, fail_d);

        if (p->last) {
                break;
        }
        p++;
    };

    if ( fail_count ) {
        printf("subtract test failed %d tests\n", fail_count );
    } else {
        puts("subtract test succeeded\n");
    }
    return fail_count;
}


/*
 * show examples of how integers and floats fail
 *
 */
static void ex_precision(void)
{
        format_test_t *p = format_tests;

        puts( "\n\n  Simple conversion examples\n\n"
                "ts:  timespec\n"
                "l32: 32 bit long\n"
                "l64: 64 bit long\n"
                "f:   float\n"
                "d:   double\n\n");

        while ( 1 ) {
            float f;
            double d;
            int32_t l32;
            int64_t l64;
            char buf_ts[TIMESPEC_LEN];
            char buf_l32[TIMESPEC_LEN];
            char buf_l64[TIMESPEC_LEN];
            char buf_f[TIMESPEC_LEN];
            char buf_d[TIMESPEC_LEN];
            const char *fail_ts = "";
            const char *fail_l32 = "";
            const char *fail_l64 = "";
            const char *fail_f = "";
            const char *fail_d = "";

            struct timespec *v = &(p->input);
            struct timespec ts_l32;
            struct timespec ts_l64;


            /* convert to test size */
            l32 = (int32_t)(v->tv_sec * NS_IN_SEC)+(int32_t)v->tv_nsec;
            l64 = (int64_t)(v->tv_sec * NS_IN_SEC)+(int64_t)v->tv_nsec;
            f = (float)TSTONS( v );
            d = TSTONS( v );

            /* now convert to strings */
            timespec_str( v, buf_ts, sizeof(buf_ts) );
            ns_to_timespec( ts_l32, l32);
            timespec_str( &ts_l32, buf_l32, sizeof(buf_l32) );
            ns_to_timespec( ts_l64, l64);
            timespec_str( &ts_l64, buf_l64, sizeof(buf_l64) );
            d_str( f, buf_f, sizeof(buf_f) );
            d_str( d, buf_d, sizeof(buf_d) );

            /* test strings */
            if ( strcmp( buf_ts, p->expected) ) {
                fail_ts = "FAIL";
            }
            if ( strcmp( buf_l32, p->expected) ) {
                fail_l32 = "FAIL";
            }
            if ( strcmp( buf_l64, p->expected) ) {
                fail_l64 = "FAIL";
            }
            if ( strcmp( buf_f, p->expected) ) {
                fail_f = "FAIL";
            }
            if ( strcmp( buf_d, p->expected) ) {
                fail_d = "FAIL";
            }
            printf( "ts:  %21s %s\n"
                    "l32: %21lld %s\n"
                    "l64: %21lld %s\n"
                    "f:   %21.9f %s\n"
                    "d:   %21.9f %s\n\n",
                        buf_ts, fail_ts,
                        (long long)l32, fail_l32,
                        (long long)l64, fail_l64,
                        f, fail_f,
                        d, fail_d);

            if ( p->last ) {
                    break;
            }
            p++;
        }

        printf( "\n\nSubtraction examples:\n");

        ex_subtract_float();
}

// parse_uri tests

struct test_parse_uri_dest_t {
    char *uri;
    char *host;
    char *service;
    char *device;
} test_parse_uri_dest_t;

// prefixed with gpsd:// in situ
struct test_parse_uri_dest_t tests_parse_uri_dest[] = {
    {"localhost", "localhost", NULL, NULL},
    {"localhost/", "localhost", NULL, NULL},
    {"localhost:", "localhost", NULL, NULL},
    {"localhost::", "localhost", NULL, NULL},
    {"localhost::/dev/ttyAMA0", "localhost", NULL, "/dev/ttyAMA0"},
    {"localhost:2947:/dev/ttyAMA0", "localhost", "2947", "/dev/ttyAMA0"},
    {"localhost:2947", "localhost", "2947", NULL},
    {"localhost:2947/", "localhost", "2947", NULL},
    {"localhost:gpsd", "localhost", "gpsd", NULL},
    {"localhost:gpsd/", "localhost", "gpsd", NULL},
    {"gpsd.io", "gpsd.io", NULL, NULL},
    {"gpsd.io/", "gpsd.io", NULL, NULL},
    {"gpsd.io:", "gpsd.io", NULL, NULL},
    {"gpsd.io::", "gpsd.io", NULL, NULL},
    {"gpsd.io::/dev/ttyAMA0", "gpsd.io", NULL, "/dev/ttyAMA0"},
    {"gpsd.io:2947:/dev/ttyAMA0", "gpsd.io", "2947", "/dev/ttyAMA0"},
    {"gpsd.io:2947", "gpsd.io", "2947", NULL},
    {"gpsd.io:2947/", "gpsd.io", "2947", NULL},
    {"gpsd.io:gpsd", "gpsd.io", "gpsd", NULL},
    {"gpsd.io:gpsd/", "gpsd.io", "gpsd", NULL},
    {"127.0.0.1", "127.0.0.1", NULL, NULL},
    {"127.0.0.1/", "127.0.0.1", NULL, NULL},
    {"127.0.0.1:", "127.0.0.1", NULL, NULL},
    {"127.0.0.1::", "127.0.0.1", NULL, NULL},
    {"127.0.0.1::/dev/ttyAMA0", "127.0.0.1", NULL, "/dev/ttyAMA0"},
    {"127.0.0.1:2947", "127.0.0.1", "2947", NULL},
    {"127.0.0.1:2947/", "127.0.0.1", "2947", NULL},
    {"127.0.0.1:gpsd", "127.0.0.1", "gpsd", NULL},
    {"127.0.0.1:gpsd/", "127.0.0.1", "gpsd", NULL},
    {"[fe80::1]", "fe80::1", NULL, NULL},
    {"[fe80::1]/", "fe80::1", NULL, NULL},
    {"[fe80::1]:", "fe80::1", NULL, NULL},
    {"[fe80::1]::", "fe80::1", NULL, NULL},
    {"[fe80::1]::/dev/ttyAMA0", "fe80::1", NULL, "/dev/ttyAMA0"},
    {"[fe80::1]:2947", "fe80::1", "2947", NULL},
    {"[fe80::1]:2947/", "fe80::1", "2947", NULL},
    {"[fe80::1]:gpsd", "fe80::1", "gpsd", NULL},
    {"[fe80::1]:gpsd/", "fe80::1", "gpsd", NULL},
    {NULL, NULL, NULL, NULL},
};

static int test_parse_uri_dest(int verbose)
{
    int fail_count = 0;
    char *host, *service, *device;
    struct test_parse_uri_dest_t *p = tests_parse_uri_dest;
    char uri[40];

    printf("\n\nTest parse_uri_dest()\n");

    while(NULL != p->uri) {
        int result;

        // parse_uri_dest() is destructive, so make a copy
        strlcpy(uri, p->uri, sizeof(uri));

        result = parse_uri_dest(uri, &host, &service, &device);
        if (0 != strcmp(p->host, host)) {
            result = 1;
        }
        if (NULL == service || NULL == p->service) {
            if (service != p->service) {
                result = 2;
            }
        } else if (0 != strcmp(p->service, service)) {
            result = 3;
        }
        if (NULL == device || NULL == p->device) {
            if (device != p->device) {
                result = 4;
            }
        } else if (0 != strcmp(p->device, device)) {
            result = 5;
        }

        if (0 != result) {
            printf("parse_uri_dest(%s, %s, %s, %s) failed %d\n", p->uri, host,
                   service ? service : "NULL",
                   device ? device : "NULL",
                   result);
            printf("  s/b parse_uri_dest(%s, %s, %s, %s) = 0\n", p->uri,
                   p->host,
                   p->service ? p->service : "NULL",
                   p->device ? p->device : "NULL");
            fail_count++;
        } else if (verbose) {
            printf("parse_uri_dest(%s, %s, %s, %s)\n", p->uri, host,
                   service ? service : "NULL",
                   device ? device : "NULL");
        }
        p++;
    }
    if (fail_count) {
        printf("parse_uri_dest() test failed %d tests\n", fail_count);
    } else {
        puts("parse_uri_dest() test succeeded\n");
    }
    return fail_count;
}

// ntrip_parse_url() tests

struct test_ntrip_parse_url_t {
    char *testurl;   // test url
    char *url;       // recovered url
    char *credentials;     // user@pass
    char *host;
    char *port;
    char *mountpoint;
    int result;
} tests_ntrip_parse_url[] = {
    // missing mountpoint
    {"ntrip.com/",
     "ntrip.com/", "", "ntrip.com", "rtcm-sc104", "MP", -1},
    // IPv4 and mountpoint
    {"127.0.0.1/MP",
     "127.0.0.1/MP", "", "127.0.0.1", "rtcm-sc104", "MP", 0},
    // IPv6 and mountpoint
    {"[fe80::1]/MP",
     "[fe80::1]/MP", "", "fe80::1", "rtcm-sc104", "MP", 0},
    // IPv6, port and mountpoint
    {"[fe80::1]:999/MP",
     "[fe80::1]:999/MP", "", "fe80::1", "999", "MP", 0},
    {"ntrip.com/MP",
     "ntrip.com/MP", "", "ntrip.com", "rtcm-sc104", "MP", 0},
    {"ntrip.com:2101/MP",
     "ntrip.com:2101/MP", "", "ntrip.com", "2101", "MP", 0},
    {"user:pass@ntrip.com/MP",
     "user:pass@ntrip.com/MP", "user:pass", "ntrip.com",
     "rtcm-sc104", "MP", 0},
    {"user:pass@ntrip.com:2101/MP",
     "user:pass@ntrip.com:2101/MP", "user:pass", "ntrip.com",
     "2101", "MP", 0},
    {"user:pass@[fe80::1]:2101/MP",
     "user:pass@[fe80::1]:2101/MP", "user:pass", "fe80::1",
     "2101", "MP", 0},
    // @ in username
    {"u@b.com:pass@ntrip.com/MP",
     "u@b.com:pass@ntrip.com/MP", "u@b.com:pass", "ntrip.com",
     "rtcm-sc104", "MP",
      0},
    {"u@b.com:pass@ntrip.com:2101/MP",
     "u@b.com:pass@ntrip.com:2101/MP", "u@b.com:pass", "ntrip.com",
     "2101", "MP", 0},
    // @ in password
    {"u@b.com:p@ss@ntrip.com/MP",
     "u@b.com:p@ss@ntrip.com/MP", "u@b.com:p@ss", "ntrip.com",
     "rtcm-sc104", "MP", 0},
    {"u@b.com:pass@ntrip.com:2101/MP",
     "u@b.com:pass@ntrip.com:2101/MP", "u@b.com:pass", "ntrip.com",
     "2101", "MP",
     0},
    // @ in password, IPv6
    {"u@b.com:p@ss@[fe80::1]/MP",
     "u@b.com:p@ss@[fe80::1]/MP", "u@b.com:p@ss", "fe80::1",
     "rtcm-sc104", "MP", 0},
    {"u@b.com:pass@[fe80::1]:2101/MP",
     "u@b.com:pass@[fe80::1]:2101/MP", "u@b.com:pass", "fe80::1",
     "2101", "MP",
     0},
    // illegal trailing slash
    {"u@b.com:pass@ntrip.com:2101/MP/",
     "u@b.com:pass@ntrip.com:2101/MP/", "u@b.com:pass", "ntrip.com",
     "2101", "MP", -1},
    // illegal trailing slash (missing mountpoint
    {"u@b.com:pass@ntrip.com:2101/",
     "u@b.com:pass@ntrip.com:2101/", "u@b.com:pass", "ntrip.com",
     "2101", "MP", -1},
    {NULL, NULL, NULL, NULL, NULL},
};

static int test_ntrip_parse_url(int verbose)
{
    int fail_count = 0;
    struct test_ntrip_parse_url_t *p = tests_ntrip_parse_url;
    struct gpsd_errout_t errout;
    struct ntrip_stream_t stream;

    printf("\n\nTest ntrip_parse_url()\n");
    memset(&errout, 0, sizeof(errout));
    errout.debug = INT_MIN;             // turn off error reporting
    errout.label = "test";              // turn off error reporting

    while(NULL != p->testurl) {
        int result;
        int err = 0;

        memset(&stream, 0, sizeof(stream));
        result = ntrip_parse_url(&errout, &stream, p->testurl);
        if (p->result != result) {
            err = 1;
        }
        if (0 == result) {
            if (0 != strcmp(p->url, stream.url)) {
                err = 2;
            }
            if (0 != strcmp(p->credentials, stream.credentials)) {
                err = 3;
            }
            if (0 != strcmp(p->port, stream.port)) {
                // Debian does not have rtcm-sc104 in /etc/services...
                if (0 != strcmp(p->port, "rtcm-sc104") ||
                    0 != strcmp(stream.port, DEFAULT_RTCM_PORT)) {
                    // so accept 2101 for rtcm-sc104
                    err = 4;
                }
            }
            if (0 != strcmp(p->host, stream.host)) {
                err = 5;
            }
            if (0 != strcmp(p->mountpoint, stream.mountpoint)) {
                err = 6;
            }
        }

        if (0 < err) {
            printf("ntrip_parse_url(%s) = %d failed err = %d\n",
                   p->testurl, result, err);
            printf("  got = %d: %s, %s, %s, %s, %s \n",
                   result,
                   stream.url,
                   stream.credentials,
                   stream.host,       // host
                   stream.port,
                   stream.mountpoint);
            printf("  s/b = %d: %s, %s, %s, %s, %s\n",
                   p->result,
                   p->url,
                   p->credentials,
                   p->host,
                   p->port,
                   p->mountpoint);
            fail_count++;
        } else if (verbose) {
            printf("  ntrip_parse_url(%s) = %d %s, %s, %s, %s, %s\n",
                   p->testurl,
                   p->result,
                   p->url,
                   p->credentials,
                   p->host,
                   p->port,
                   p->mountpoint);
        }
        p++;
    }
    if (fail_count) {
        printf("ntrip_parse_url() test failed %d tests\n", fail_count);
    } else {
        puts("ntrip_parse_url() test succeeded\n");
    }
    return fail_count;
}

int main(int argc, char *argv[])
{
    int fail_count = 0;
    int verbose = 0;
    int option;

    while ((option = getopt(argc, argv, "h?vV")) != -1) {
        switch (option) {
        default:
                fail_count = 1;
                FALLTHROUGH
        case '?':
                FALLTHROUGH
        case 'h':
            (void)fputs("usage: test_timespec [-v] [-V]\n", stderr);
            exit(fail_count);
        case 'V':
            (void)fprintf( stderr, "test_timespec %s\n", VERSION);
            exit(EXIT_SUCCESS);
        case 'v':
            verbose = 1;
            break;
        }
    }


    fail_count = test_format(verbose);
    fail_count += test_ts_to_ms(verbose);
    fail_count += test_ts_subtract(verbose);
    fail_count += test_ns_subtract(verbose);
    fail_count += test_gpsd_gpstime_resolv(verbose);
    fail_count += test_parse_uri_dest(verbose);
    fail_count += test_ntrip_parse_url(verbose);

    if ( fail_count ) {
        printf("timespec tests failed %d tests\n", fail_count );
        exit(1);
    }
    printf("timespec tests succeeded\n");

    if (verbose) {
        ex_precision();
    }
    exit(0);
}
// vim: set expandtab shiftwidth=4

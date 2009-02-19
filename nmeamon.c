#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>

#include "gpsd_config.h"
#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif /* HAVE_NCURSES_H */
#include "gpsd.h"

#include "bits.h"
#include "gpsmon.h"

extern const struct gps_type_t nmea;

static WINDOW *nmeawin, *satwin, *gprmcwin, *gpggawin, *gpgsawin;
static double last_tick, tick_interval;

#define SENTENCELINE 1

static bool nmea_initialize(void)
{
    int i;

    /*@ -onlytrans @*/
    nmeawin = derwin(devicewin, 3,  80, 0, 0);
    (void)wborder(nmeawin, 0, 0, 0, 0, 0, 0, 0, 0);
    (void)syncok(nmeawin, true);

    wattrset(nmeawin, A_BOLD);
    mvwaddstr(nmeawin, 2, 34, " Sentences ");
    wattrset(nmeawin, A_NORMAL);

    satwin  = derwin(devicewin, 15, 20, 3, 0);
    (void)wborder(satwin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(satwin, true);
    (void)wattrset(satwin, A_BOLD);
    (void)mvwprintw(satwin, 1, 1, " Ch SV  Az El S/N");
    for (i = 0; i < SIRF_CHANNELS; i++)
	(void)mvwprintw(satwin, (int)(i+2), 1, "%2d",i);
    (void)mvwprintw(satwin, 14, 7, " GSV ");
    (void)wattrset(satwin, A_NORMAL);

    gprmcwin  = derwin(devicewin, 8, 30, 3, 20);
    (void)wborder(gprmcwin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(gprmcwin, true);
    (void)wattrset(gprmcwin, A_BOLD);
    (void)mvwprintw(gprmcwin, 1, 1, "Time: ");
    (void)mvwprintw(gprmcwin, 2, 1, "Latitude: ");
    (void)mvwprintw(gprmcwin, 3, 1, "Longitude: ");
    (void)mvwprintw(gprmcwin, 4, 1, "Speed: ");
    (void)mvwprintw(gprmcwin, 5, 1, "Course: ");
    (void)mvwprintw(gprmcwin, 6, 1, "Status:          FAA: ");
    (void)mvwprintw(gprmcwin, 7, 12, " RMC ");
    (void)wattrset(gprmcwin, A_NORMAL);

    gpgsawin  = derwin(devicewin, 7, 30, 11, 20);
    (void)wborder(gpgsawin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(gpgsawin, true);
    (void)wattrset(gpgsawin, A_BOLD);
    (void)mvwprintw(gpgsawin, 1, 1, "Mode: ");
    (void)mvwprintw(gpgsawin, 2, 1, "Sats: ");
    (void)mvwprintw(gpgsawin, 3, 1, "HDOP: ");
    (void)mvwprintw(gpgsawin, 4, 1, "VDOP: ");
    (void)mvwprintw(gpgsawin, 5, 1, "PDOP: ");
    (void)mvwprintw(gpgsawin, 6, 12, " GSA ");
    (void)wattrset(gpgsawin, A_NORMAL);

    gpggawin  = derwin(devicewin, 6, 30, 3, 50);
    (void)wborder(gpggawin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(gpggawin, true);
    (void)wattrset(gpggawin, A_BOLD);
    (void)mvwprintw(gpggawin, 1, 1, "Altitude: ");
    (void)mvwprintw(gpggawin, 2, 1, "Quality:    Sats:   ");
    (void)mvwprintw(gpggawin, 3, 1, "HDOP: ");
    (void)mvwprintw(gpggawin, 4, 1, "Geoid: ");
    (void)mvwprintw(gpggawin, 5, 12, " GGA ");
    (void)wattrset(gpggawin, A_NORMAL);
    /*@ +onlytrans @*/

    last_tick = timestamp();

    return (nmeawin != NULL);
}

/*@ -globstate -nullpass (splint is confused) */
static void nmea_update(void)
{
    static char sentences[NMEA_MAX];
    char **fields;

    assert(nmeawin!=NULL);
    assert(gpgsawin!=NULL);
    assert(gpggawin!=NULL);
    assert(gprmcwin!=NULL);

    fields = session.driver.nmea.field;

    if (session.packet.outbuffer[0] == (unsigned char)'$') {
	int ymax, xmax;
	double now;
	char newid[NMEA_MAX];
	getmaxyx(nmeawin, ymax, xmax);
	(void)strlcpy(newid, (char *)session.packet.outbuffer+1, 
		      strcspn((char *)session.packet.outbuffer+1, ",")+1);
	if (strstr(sentences, newid) == NULL) {
	    char *s_end = sentences + strlen(sentences);
	    if ((int)(strlen(sentences) + strlen(newid)) < xmax-2) {
		*s_end++ = ' '; 
		(void)strcpy(s_end, newid);
	    } else {
		*--s_end = '.';
		*--s_end = '.';
		*--s_end = '.';
	    }
	    mvwaddstr(nmeawin, SENTENCELINE, 1, sentences);
	}

	/* 
	 * If the interval between this and last update is 
	 * the longest we've seen yet, boldify the corresponding
	 * tag.
	 */
	now = timestamp();
	if (now > last_tick && (now - last_tick) > tick_interval)
	{
	    char *findme = strstr(sentences, newid);

	    tick_interval = now - last_tick;
	    if (findme != NULL) {
		mvwchgat(nmeawin, SENTENCELINE, 1, xmax-13, A_NORMAL, 0, NULL);
		mvwchgat(nmeawin, 
			 SENTENCELINE, 1+(findme-sentences), 
			 (int)strlen(newid),
			 A_BOLD, 0, NULL);
	    }
	}
	last_tick = now;

	if (strcmp(newid, "GPGSV") == 0) {
	    int i;

	    for (i = 0; i < session.gpsdata.satellites; i++) {
		(void)wmove(satwin, i+2, 3);
		(void)wprintw(satwin, " %3d %3d%3d %3d", 
			      session.gpsdata.PRN[i],
			      session.gpsdata.azimuth[i],
			      session.gpsdata.elevation[i],
			      session.gpsdata.ss[i]);
	    }
	}

	if (strcmp(newid, "GPRMC") == 0) {
	    /* Not dumped yet: magnetic variation */
	    char scr[128];
	    if (isnan(session.gpsdata.fix.time)==0) {
		(void)unix_to_iso8601(session.gpsdata.fix.time, scr, sizeof(scr));
	    } else
		(void)snprintf(scr, sizeof(scr), "n/a");
	    (void)wmove(gprmcwin, 1, 7);
	    (void)wclrtoeol(gprmcwin);
	    (void)waddstr(gprmcwin, scr);
	    fixframe(gprmcwin);

	    /* Fill in the latitude. */
	    if (session.gpsdata.fix.mode >= MODE_2D && isnan(session.gpsdata.fix.latitude)==0) {
		(void)snprintf(scr, sizeof(scr), "%s %c", 
			       deg_to_str(deg_ddmmss,  fabs(session.gpsdata.fix.latitude)), 
			       (session.gpsdata.fix.latitude < 0) ? 'S' : 'N');
	    } else
		(void)snprintf(scr, sizeof(scr), "n/a");
	    (void)mvwprintw(gprmcwin, 2, 11, "%-17s", scr);

	    /* Fill in the longitude. */
	    if (session.gpsdata.fix.mode >= MODE_2D && isnan(session.gpsdata.fix.longitude)==0) {
		(void)snprintf(scr, sizeof(scr), "%s %c", 
			       deg_to_str(deg_ddmmss,  fabs(session.gpsdata.fix.longitude)), 
			       (session.gpsdata.fix.longitude < 0) ? 'W' : 'E');
	    } else
		(void)snprintf(scr, sizeof(scr), "n/a");
	    (void)mvwprintw(gprmcwin, 3, 11, "%-17s", scr);

	    /* Fill in the speed. */
	    if (session.gpsdata.fix.mode >= MODE_2D && isnan(session.gpsdata.fix.track)==0)
		(void)snprintf(scr, sizeof(scr), "%.1f meters/sec", session.gpsdata.fix.speed);
	    else
		(void)snprintf(scr, sizeof(scr), "n/a");
	    (void)mvwprintw(gprmcwin, 4, 11, "%-17s", scr);

	    if (session.gpsdata.fix.mode >= MODE_2D && isnan(session.gpsdata.fix.track)==0)
		(void)snprintf(scr, sizeof(scr), "%.1f deg", session.gpsdata.fix.track);
	    else
		(void)snprintf(scr, sizeof(scr), "n/a");
	    (void)mvwprintw(gprmcwin, 5, 11, "%-17s", scr);

	    /* the status field and FAA code */
	    (void)mvwaddstr(gprmcwin, 6, 11, fields[2]);
	    (void)mvwaddstr(gprmcwin, 6, 23, fields[12]);
	}

	if (strcmp(newid, "GPGSA") == 0) {
	    char scr[128];
	    int i;
	    (void)mvwprintw(gpgsawin, 1,7, "%1s %s", fields[1], fields[2]);
	    (void)wmove(gpgsawin, 2, 7);
	    (void)wclrtoeol(gpgsawin);
	    scr[0] = '\0';
	    for (i = 0; i < session.gpsdata.satellites_used; i++) {
		(void)snprintf(scr + strlen(scr), sizeof(scr)-strlen(scr), 
			       "%d ", session.gpsdata.used[i]);
	    }
	    getmaxyx(gpgsawin, ymax, xmax);
	    (void)mvwaddnstr(gpgsawin, 2, 7, scr, xmax-2-7);
	    if (strlen(scr) >= (size_t)(xmax-2)) {
		mvwaddch(gpgsawin, 2, xmax-2-7, (chtype)'.');
		mvwaddch(gpgsawin, 2, xmax-3-7, (chtype)'.');
		mvwaddch(gpgsawin, 2, xmax-4-7, (chtype)'.');
	    }
	    fixframe(gpgsawin);
	    (void)wmove(gpgsawin, 3, 7); 
	    (void)wprintw(gpgsawin, "%2.2f", session.gpsdata.hdop);
	    (void)wmove(gpgsawin, 4, 7); 
	    (void)wprintw(gpgsawin, "%2.2f", session.gpsdata.vdop);
	    (void)wmove(gpgsawin, 5, 7); 
	    (void)wprintw(gpgsawin, "%2.2f", session.gpsdata.pdop);
	}
	if (strcmp(newid, "GPGGA") == 0) {
	    char scr[128];
	    /* Fill in the altitude. */
	    if (session.gpsdata.fix.mode == MODE_3D && isnan(session.gpsdata.fix.altitude)==0)
		(void)snprintf(scr, sizeof(scr), "%.1f meters",session.gpsdata.fix.altitude);
	    else
		(void)snprintf(scr, sizeof(scr), "n/a");
	    (void)mvwprintw(gpggawin, 1, 11, "%-17s", scr);
	    (void)mvwprintw(gpggawin, 2, 10, "%1.1s", fields[6]);
	    (void)mvwprintw(gpggawin, 2, 19, "%2.2s", fields[7]);
	    (void)mvwprintw(gpggawin, 3, 10, "%-5.5s", fields[8]);
	    (void)mvwprintw(gpggawin, 4, 10, "%-5.5s", fields[11]);
	}
    }
}
/*@ +globstate +nullpass */

#undef SENTENCELINE

static void nmea_wrap(void)
{
    (void)delwin(nmeawin);
    (void)delwin(gpgsawin);
    (void)delwin(gpggawin);
    (void)delwin(gprmcwin);
}

const struct monitor_object_t nmea_mmt = {
    .initialize = nmea_initialize,
    .update = nmea_update,
    .command = NULL,
    .wrap = nmea_wrap,
    .min_y = 18, .min_x = 80,
    .driver = &nmea,
};

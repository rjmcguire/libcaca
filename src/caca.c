/*
 *   libcaca       ASCII-Art library
 *   Copyright (c) 2002, 2003 Sam Hocevar <sam@zoy.org>
 *                 All Rights Reserved
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *   02111-1307  USA
 */

/**  \file caca.c
 *   \version \$Id$
 *   \author Sam Hocevar <sam@zoy.org>
 *   \brief Main \e libcaca functions
 *
 *   This file contains the main functions used by \e libcaca applications to
 *   initialise the library, get the screen properties, set the framerate and
 *   so on.
 */

#include "config.h"

#if defined(USE_SLANG)
#   include <slang.h>
#elif defined(USE_NCURSES)
#   include <curses.h>
#elif defined(USE_CONIO)
#   include <dos.h>
#   include <conio.h>
#else
#   error "no graphics library detected"
#endif

#include <stdlib.h>
#include <string.h>

#include "caca.h"
#include "caca_internals.h"

static void caca_init_features(void);
static void caca_init_terminal(void);

#if defined(USE_NCURSES)
static mmask_t oldmask;
#endif

int caca_init(void)
{
#if defined(USE_NCURSES)
    mmask_t newmask;
#endif

    caca_init_features();
    caca_init_terminal();

#if defined(USE_SLANG)
    /* Initialize slang library */
    SLsig_block_signals();
    SLtt_get_terminfo();

    if(SLkp_init() == -1)
    {
        SLsig_unblock_signals();
        return -1;
    }

    SLang_init_tty(-1, 0, 1);

    if(SLsmg_init_smg() == -1)
    {
        SLsig_unblock_signals();
        return -1;
    }

    SLsig_unblock_signals();

    SLsmg_cls();
    SLtt_set_cursor_visibility(0);
    SLkp_define_keysym("\e[M", 1001);
    SLtt_set_mouse_mode(1, 0);
    SLsmg_refresh();

    /* Disable scrolling so that hashmap scrolling optimization code
     * does not cause ugly refreshes due to slow terminals */
    SLtt_Term_Cannot_Scroll = 1;

#elif defined(USE_NCURSES)
    initscr();
    keypad(stdscr, TRUE);
    nonl();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    curs_set(0);

    /* Activate mouse */
    newmask = ALL_MOUSE_EVENTS;
    mousemask(newmask, &oldmask);

#elif defined(USE_CONIO)
    _wscroll = 0;
    _setcursortype(_NOCURSOR);
    clrscr();

#endif
    if(_caca_init_graphics())
        return -1;

    return 0;
}

unsigned int caca_get_width(void)
{
    return _caca_width;
}

unsigned int caca_get_height(void)
{
    return _caca_height;
}

const char *caca_get_color_name(enum caca_color color)
{
    static const char *color_names[] =
    {
        "black",
        "blue",
        "green",
        "cyan",
        "red",
        "magenta",
        "brown",
        "light gray",
        "dark gray",
        "light blue",
        "light green",
        "light cyan",
        "light red",
        "light magenta",
        "yellow",
        "white",
    };

    if(color < 0 || color > 15)
        return "unknown";

    return color_names[color];
}

enum caca_feature caca_get_feature(enum caca_feature feature)
{
    switch(feature)
    {
        case CACA_BACKGROUND:
            return _caca_background;
        case CACA_ANTIALIASING:
            return _caca_antialiasing;
        case CACA_DITHERING:
            return _caca_dithering;

        default:
            return CACA_UNKNOWN_FEATURE;
    }
}

void caca_set_feature(enum caca_feature feature)
{
    switch(feature)
    {
        case CACA_BACKGROUND:
            feature = CACA_BACKGROUND_SOLID;
        case CACA_BACKGROUND_BLACK:
        case CACA_BACKGROUND_SOLID:
            _caca_background = feature;
            break;

        case CACA_ANTIALIASING:
            feature = CACA_ANTIALIASING_PREFILTER;
        case CACA_ANTIALIASING_NONE:
        case CACA_ANTIALIASING_PREFILTER:
            _caca_antialiasing = feature;
            break;

        case CACA_DITHERING:
            feature = CACA_DITHERING_ORDERED4;
        case CACA_DITHERING_NONE:
        case CACA_DITHERING_ORDERED2:
        case CACA_DITHERING_ORDERED4:
        case CACA_DITHERING_ORDERED8:
        case CACA_DITHERING_RANDOM:
            _caca_dithering = feature;
            break;

        case CACA_UNKNOWN_FEATURE:
            break;
    }
}

const char *caca_get_feature_name(enum caca_feature feature)
{
    switch(feature)
    {
        case CACA_BACKGROUND_BLACK: return "black background";
        case CACA_BACKGROUND_SOLID: return "solid background";

        case CACA_ANTIALIASING_NONE:      return "no antialiasing";
        case CACA_ANTIALIASING_PREFILTER: return "prefilter antialiasing";

        case CACA_DITHERING_NONE:     return "no dithering";
        case CACA_DITHERING_ORDERED2: return "2x2 ordered dithering";
        case CACA_DITHERING_ORDERED4: return "4x4 ordered dithering";
        case CACA_DITHERING_ORDERED8: return "8x8 ordered dithering";
        case CACA_DITHERING_RANDOM:   return "random dithering";

        default: return "unknown";
    }
}

void caca_end(void)
{
#if defined(USE_SLANG)
    SLtt_set_mouse_mode(0, 0);
    SLtt_set_cursor_visibility(1);
    SLang_reset_tty();
    SLsmg_reset_smg();
#elif defined(USE_NCURSES)
    mousemask(oldmask, NULL);
    curs_set(1);
    endwin();
#elif defined(USE_CONIO)
    _wscroll = 1;
    textcolor((enum COLORS)WHITE);
    textbackground((enum COLORS)BLACK);
    gotoxy(_caca_width, _caca_height);
    cputs("\r\n");
    _setcursortype(_NORMALCURSOR);
#endif
}

static void caca_init_features(void)
{
    /* FIXME: if strcasecmp isn't available, use strcmp */
#if defined(HAVE_GETENV) && defined(HAVE_STRCASECMP)
    char *var;
#endif

    caca_set_feature(CACA_BACKGROUND);
    caca_set_feature(CACA_ANTIALIASING);
    caca_set_feature(CACA_DITHERING);

#if defined(HAVE_GETENV) && defined(HAVE_STRCASECMP)
    if((var = getenv("CACA_BACKGROUND")))
    {
        if(!strcasecmp("black", var))
            caca_set_feature(CACA_BACKGROUND_BLACK);
        else if(!strcasecmp("solid", var))
            caca_set_feature(CACA_BACKGROUND_SOLID);
    }

    if((var = getenv("CACA_ANTIALIASING")))
    {
        if(!strcasecmp("none", var))
            caca_set_feature(CACA_ANTIALIASING_NONE);
        else if(!strcasecmp("prefilter", var))
            caca_set_feature(CACA_ANTIALIASING_PREFILTER);
    }

    if((var = getenv("CACA_DITHERING")))
    {
        if(!strcasecmp("none", var))
            caca_set_feature(CACA_DITHERING_NONE);
        else if(!strcasecmp("ordered2", var))
            caca_set_feature(CACA_DITHERING_ORDERED2);
        else if(!strcasecmp("ordered4", var))
            caca_set_feature(CACA_DITHERING_ORDERED4);
        else if(!strcasecmp("ordered8", var))
            caca_set_feature(CACA_DITHERING_ORDERED8);
        else if(!strcasecmp("random", var))
            caca_set_feature(CACA_DITHERING_RANDOM);
    }
#endif
}

static void caca_init_terminal(void)
{
#if defined(HAVE_GETENV) && defined(HAVE_PUTENV)
    char *term, *colorterm, *other;

    term = getenv("TERM");
    colorterm = getenv("COLORTERM");

    if(term && !strcmp(term, "xterm"))
    {
        /* If we are using gnome-terminal, it's really a 16 colour terminal */
        if(colorterm && !strcmp(colorterm, "gnome-terminal"))
        {
#if defined(USE_NCURSES)
            SCREEN *screen;
            screen = newterm("xterm-16color", stdout, stdin);
            if(screen == NULL)
                return;
            endwin();
#endif
            (void)putenv("TERM=xterm-16color");
            return;
        }

        /* Ditto if we are using Konsole */
        other = getenv("KONSOLE_DCOP_SESSION");
        if(other)
        {
#if defined(USE_NCURSES)
            SCREEN *screen;
            screen = newterm("xterm-16color", stdout, stdin);
            if(screen == NULL)
                return;
            endwin();
#endif
            (void)putenv("TERM=xterm-16color");
            return;
        }
    }
#endif
}


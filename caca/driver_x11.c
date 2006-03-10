/*
 *  libcaca       ASCII-Art library
 *  Copyright (c) 2002-2006 Sam Hocevar <sam@zoy.org>
 *                All Rights Reserved
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the Do What The Fuck You Want To
 *  Public License, Version 2, as published by Sam Hocevar. See
 *  http://sam.zoy.org/wtfpl/COPYING for more details.
 */

/** \file driver_x11.c
 *  \version \$Id$
 *  \author Sam Hocevar <sam@zoy.org>
 *  \brief X11 driver
 *
 *  This file contains the libcaca X11 input and output driver
 */

#include "config.h"

#if defined(USE_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#if defined(HAVE_X11_XKBLIB_H)
#   include <X11/XKBlib.h>
#endif

#include <stdio.h> /* BUFSIZ */
#include <stdlib.h>

#include "caca.h"
#include "caca_internals.h"
#include "cucul.h"
#include "cucul_internals.h"

/*
 * Local functions
 */
static int x11_error_handler(Display *, XErrorEvent *);

struct driver_private
{
    Display *dpy;
    Window window;
    Pixmap pixmap;
    GC gc;
    long int event_mask;
    int font_width, font_height;
    int colors[16];
    Font font;
    XFontStruct *font_struct;
    int font_offset;
#if defined(HAVE_X11_XKBLIB_H)
    Bool autorepeat;
#endif
};

static int x11_init_graphics(caca_t *kk)
{
    static int const x11_palette[] =
    {
        /* Standard curses colours */
        0x0,    0x0,    0x0,
        0x0,    0x0,    0x8000,
        0x0,    0x8000, 0x0,
        0x0,    0x8000, 0x8000,
        0x8000, 0x0,    0x0,
        0x8000, 0x0,    0x8000,
        0x8000, 0x8000, 0x0,
        0x8000, 0x8000, 0x8000,
        /* Extra values for xterm-16color */
        0x4000, 0x4000, 0x4000,
        0x4000, 0x4000, 0xffff,
        0x4000, 0xffff, 0x4000,
        0x4000, 0xffff, 0xffff,
        0xffff, 0x4000, 0x4000,
        0xffff, 0x4000, 0xffff,
        0xffff, 0xffff, 0x4000,
        0xffff, 0xffff, 0xffff,
    };

    Colormap colormap;
    XSetWindowAttributes x11_winattr;
    int (*old_error_handler)(Display *, XErrorEvent *);
    char const *fonts[] = { NULL, "8x13bold", "fixed" }, **parser;
    char const *geometry;
    unsigned int width = 0, height = 0;
    int i;

    kk->drv.p = malloc(sizeof(struct driver_private));

    geometry = getenv("CACA_GEOMETRY");
    if(geometry && *(geometry))
        sscanf(geometry, "%ux%u", &width, &height);

    if(width && height)
        cucul_set_size(kk->qq, width, height);

    kk->drv.p->dpy = XOpenDisplay(NULL);
    if(kk->drv.p->dpy == NULL)
        return -1;

    fonts[0] = getenv("CACA_FONT");
    if(fonts[0] && *fonts[0])
        parser = fonts;
    else
        parser = fonts + 1;

    /* Ignore font errors */
    old_error_handler = XSetErrorHandler(x11_error_handler);

    /* Parse our font list */
    for( ; ; parser++)
    {
        if(!*parser)
        {
            XSetErrorHandler(old_error_handler);
            XCloseDisplay(kk->drv.p->dpy);
            return -1;
        }

        kk->drv.p->font = XLoadFont(kk->drv.p->dpy, *parser);
        if(!kk->drv.p->font)
            continue;

        kk->drv.p->font_struct = XQueryFont(kk->drv.p->dpy, kk->drv.p->font);
        if(!kk->drv.p->font_struct)
        {
            XUnloadFont(kk->drv.p->dpy, kk->drv.p->font);
            continue;
        }

        break;
    }

    /* Reset the default X11 error handler */
    XSetErrorHandler(old_error_handler);

    kk->drv.p->font_width = kk->drv.p->font_struct->max_bounds.width;
    kk->drv.p->font_height = kk->drv.p->font_struct->max_bounds.ascent
                         + kk->drv.p->font_struct->max_bounds.descent;
    kk->drv.p->font_offset = kk->drv.p->font_struct->max_bounds.descent;

    colormap = DefaultColormap(kk->drv.p->dpy, DefaultScreen(kk->drv.p->dpy));
    for(i = 0; i < 16; i++)
    {
        XColor color;
        color.red = x11_palette[i * 3];
        color.green = x11_palette[i * 3 + 1];
        color.blue = x11_palette[i * 3 + 2];
        XAllocColor(kk->drv.p->dpy, colormap, &color);
        kk->drv.p->colors[i] = color.pixel;
    }

    x11_winattr.backing_store = Always;
    x11_winattr.background_pixel = kk->drv.p->colors[0];
    x11_winattr.event_mask = ExposureMask | StructureNotifyMask;

    kk->drv.p->window =
        XCreateWindow(kk->drv.p->dpy, DefaultRootWindow(kk->drv.p->dpy), 0, 0,
                      kk->qq->width * kk->drv.p->font_width,
                      kk->qq->height * kk->drv.p->font_height,
                      0, 0, InputOutput, 0,
                      CWBackingStore | CWBackPixel | CWEventMask,
                      &x11_winattr);

    XStoreName(kk->drv.p->dpy, kk->drv.p->window, "caca for X");

    XSelectInput(kk->drv.p->dpy, kk->drv.p->window, StructureNotifyMask);
    XMapWindow(kk->drv.p->dpy, kk->drv.p->window);

    kk->drv.p->gc = XCreateGC(kk->drv.p->dpy, kk->drv.p->window, 0, NULL);
    XSetForeground(kk->drv.p->dpy, kk->drv.p->gc, kk->drv.p->colors[15]);
    XSetFont(kk->drv.p->dpy, kk->drv.p->gc, kk->drv.p->font);

    for(;;)
    {
        XEvent xevent;
        XNextEvent(kk->drv.p->dpy, &xevent);
        if (xevent.type == MapNotify)
            break;
    }

#if defined(HAVE_X11_XKBLIB_H)
    /* Disable autorepeat */
    XkbSetDetectableAutoRepeat(kk->drv.p->dpy, True, &kk->drv.p->autorepeat);
    if(!kk->drv.p->autorepeat)
        XAutoRepeatOff(kk->drv.p->dpy);
#endif

    kk->drv.p->event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask
          | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask
          | ExposureMask;

    XSelectInput(kk->drv.p->dpy, kk->drv.p->window, kk->drv.p->event_mask);

    XSync(kk->drv.p->dpy, False);

    kk->drv.p->pixmap = XCreatePixmap(kk->drv.p->dpy, kk->drv.p->window,
                                   kk->qq->width * kk->drv.p->font_width,
                                   kk->qq->height * kk->drv.p->font_height,
                                   DefaultDepth(kk->drv.p->dpy,
                                            DefaultScreen(kk->drv.p->dpy)));

    return 0;
}

static int x11_end_graphics(caca_t *kk)
{
    XSync(kk->drv.p->dpy, False);
#if defined(HAVE_X11_XKBLIB_H)
    if(!kk->drv.p->autorepeat)
        XAutoRepeatOn(kk->drv.p->dpy);
#endif
    XFreePixmap(kk->drv.p->dpy, kk->drv.p->pixmap);
    XFreeFont(kk->drv.p->dpy, kk->drv.p->font_struct);
    XFreeGC(kk->drv.p->dpy, kk->drv.p->gc);
    XUnmapWindow(kk->drv.p->dpy, kk->drv.p->window);
    XDestroyWindow(kk->drv.p->dpy, kk->drv.p->window);
    XCloseDisplay(kk->drv.p->dpy);

    free(kk->drv.p);

    return 0;
}

static int x11_set_window_title(caca_t *kk, char const *title)
{
    XStoreName(kk->drv.p->dpy, kk->drv.p->window, title);
    return 0;
}

static unsigned int x11_get_window_width(caca_t *kk)
{
    return kk->qq->width * kk->drv.p->font_width;
}

static unsigned int x11_get_window_height(caca_t *kk)
{
    return kk->qq->height * kk->drv.p->font_height;
}

static void x11_display(caca_t *kk)
{
    unsigned int x, y, len;

    /* First draw the background colours. Splitting the process in two
     * loops like this is actually slightly faster. */
    for(y = 0; y < kk->qq->height; y++)
    {
        for(x = 0; x < kk->qq->width; x += len)
        {
            uint8_t *attr = kk->qq->attr + x + y * kk->qq->width;

            len = 1;
            while(x + len < kk->qq->width
                   && (attr[len] >> 4) == (attr[0] >> 4))
                len++;

            XSetForeground(kk->drv.p->dpy, kk->drv.p->gc,
                           kk->drv.p->colors[attr[0] >> 4]);
            XFillRectangle(kk->drv.p->dpy, kk->drv.p->pixmap, kk->drv.p->gc,
                           x * kk->drv.p->font_width, y * kk->drv.p->font_height,
                           len * kk->drv.p->font_width, kk->drv.p->font_height);
        }
    }

    /* Then print the foreground characters */
    for(y = 0; y < kk->qq->height; y++)
    {
        unsigned int yoff = (y + 1) * kk->drv.p->font_height
                                    - kk->drv.p->font_offset;

        for(x = 0; x < kk->qq->width; x += len)
        {
            char buffer[BUFSIZ]; /* FIXME: use a smaller buffer */
            uint32_t *chars = kk->qq->chars + x + y * kk->qq->width;
            uint8_t *attr = kk->qq->attr + x + y * kk->qq->width;

            len = 1;

            /* Skip spaces */
            if(chars[0] <= 0x00000020 || chars[0] >= 0x00000080)
                continue;

            buffer[0] = (char)chars[0];

            while(x + len < kk->qq->width
                   && (attr[len] & 0xf) == (attr[0] & 0xf))
            {
                if(chars[len] > 0x00000020 && chars[len] < 0x00000080)
                    buffer[len] = (char)chars[len];
                else
                    buffer[len] = ' ';
                len++;
            }

            XSetForeground(kk->drv.p->dpy, kk->drv.p->gc,
                           kk->drv.p->colors[attr[0] & 0xf]);
            XDrawString(kk->drv.p->dpy, kk->drv.p->pixmap, kk->drv.p->gc,
                        x * kk->drv.p->font_width, yoff, buffer, len);
        }
    }

    XCopyArea(kk->drv.p->dpy, kk->drv.p->pixmap, kk->drv.p->window,
              kk->drv.p->gc, 0, 0,
              kk->qq->width * kk->drv.p->font_width,
              kk->qq->height * kk->drv.p->font_height,
              0, 0);
    XFlush(kk->drv.p->dpy);
}

static void x11_handle_resize(caca_t *kk)
{
    Pixmap new_pixmap;

    new_pixmap = XCreatePixmap(kk->drv.p->dpy, kk->drv.p->window,
                               kk->resize.w * kk->drv.p->font_width,
                               kk->resize.h * kk->drv.p->font_height,
                               DefaultDepth(kk->drv.p->dpy,
                                            DefaultScreen(kk->drv.p->dpy)));
    XCopyArea(kk->drv.p->dpy, kk->drv.p->pixmap, new_pixmap,
              kk->drv.p->gc, 0, 0,
              kk->resize.w * kk->drv.p->font_width,
              kk->resize.h * kk->drv.p->font_height, 0, 0);
    XFreePixmap(kk->drv.p->dpy, kk->drv.p->pixmap);
    kk->drv.p->pixmap = new_pixmap;
}

static unsigned int x11_get_event(caca_t *kk)
{
    unsigned int event = 0;
    XEvent xevent;
    char key;

    while(XCheckWindowEvent(kk->drv.p->dpy, kk->drv.p->window,
                            kk->drv.p->event_mask, &xevent) == True)
    {
        KeySym keysym;

        /* Expose event */
        if(xevent.type == Expose)
        {
            XCopyArea(kk->drv.p->dpy, kk->drv.p->pixmap,
                      kk->drv.p->window, kk->drv.p->gc, 0, 0,
                      kk->qq->width * kk->drv.p->font_width,
                      kk->qq->height * kk->drv.p->font_height, 0, 0);
            continue;
        }

        /* Resize event */
        if(xevent.type == ConfigureNotify)
        {
            unsigned int w, h;

            w = (xevent.xconfigure.width + kk->drv.p->font_width / 3)
                  / kk->drv.p->font_width;
            h = (xevent.xconfigure.height + kk->drv.p->font_height / 3)
                  / kk->drv.p->font_height;

            if(!w || !h || (w == kk->qq->width && h == kk->qq->height))
                continue;

            kk->resize.w = w;
            kk->resize.h = h;
            kk->resize.resized = 1;

            continue;
        }

        /* Check for mouse motion events */
        if(xevent.type == MotionNotify)
        {
            unsigned int newx = xevent.xmotion.x / kk->drv.p->font_width;
            unsigned int newy = xevent.xmotion.y / kk->drv.p->font_height;

            if(newx >= kk->qq->width)
                newx = kk->qq->width - 1;
            if(newy >= kk->qq->height)
                newy = kk->qq->height - 1;

            if(kk->mouse.x == newx && kk->mouse.y == newy)
                continue;

            kk->mouse.x = newx;
            kk->mouse.y = newy;

            return CACA_EVENT_MOUSE_MOTION | (kk->mouse.x << 12) | kk->mouse.y;
        }

        /* Check for mouse press and release events */
        if(xevent.type == ButtonPress)
            return CACA_EVENT_MOUSE_PRESS
                    | ((XButtonEvent *)&xevent)->button;

        if(xevent.type == ButtonRelease)
            return CACA_EVENT_MOUSE_RELEASE
                    | ((XButtonEvent *)&xevent)->button;

        /* Check for key press and release events */
        if(xevent.type == KeyPress)
            event |= CACA_EVENT_KEY_PRESS;
        else if(xevent.type == KeyRelease)
            event |= CACA_EVENT_KEY_RELEASE;
        else
            continue;

        if(XLookupString(&xevent.xkey, &key, 1, NULL, NULL))
            return event | key;

        keysym = XKeycodeToKeysym(kk->drv.p->dpy, xevent.xkey.keycode, 0);
        switch(keysym)
        {
        case XK_F1:    return event | CACA_KEY_F1;
        case XK_F2:    return event | CACA_KEY_F2;
        case XK_F3:    return event | CACA_KEY_F3;
        case XK_F4:    return event | CACA_KEY_F4;
        case XK_F5:    return event | CACA_KEY_F5;
        case XK_F6:    return event | CACA_KEY_F6;
        case XK_F7:    return event | CACA_KEY_F7;
        case XK_F8:    return event | CACA_KEY_F8;
        case XK_F9:    return event | CACA_KEY_F9;
        case XK_F10:   return event | CACA_KEY_F10;
        case XK_F11:   return event | CACA_KEY_F11;
        case XK_F12:   return event | CACA_KEY_F12;
        case XK_F13:   return event | CACA_KEY_F13;
        case XK_F14:   return event | CACA_KEY_F14;
        case XK_F15:   return event | CACA_KEY_F15;
        case XK_Left:  return event | CACA_KEY_LEFT;
        case XK_Right: return event | CACA_KEY_RIGHT;
        case XK_Up:    return event | CACA_KEY_UP;
        case XK_Down:  return event | CACA_KEY_DOWN;
        default:       return CACA_EVENT_NONE;
        }
    }

    return CACA_EVENT_NONE;
}

/*
 * XXX: following functions are local
 */

static int x11_error_handler(Display *dpy, XErrorEvent *xevent)
{
    /* Ignore the error */
    return 0;
}

/*
 * Driver initialisation
 */

void x11_init_driver(caca_t *kk)
{
    kk->drv.driver = CACA_DRIVER_X11;

    kk->drv.init_graphics = x11_init_graphics;
    kk->drv.end_graphics = x11_end_graphics;
    kk->drv.set_window_title = x11_set_window_title;
    kk->drv.get_window_width = x11_get_window_width;
    kk->drv.get_window_height = x11_get_window_height;
    kk->drv.display = x11_display;
    kk->drv.handle_resize = x11_handle_resize;
    kk->drv.get_event = x11_get_event;
}

#endif /* USE_X11 */

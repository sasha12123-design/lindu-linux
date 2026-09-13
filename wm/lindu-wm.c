/*
 * lindu-wm — собственный композитный оконный менеджер
 * для дистрибутива lindu linux.
 *
 * Написан с нуля на Xlib (X11) + Xinerama. Тайловый WM с тегами,
 * раскладками TILE / MONOCLE / FLOAT, встроенной панелью
 * и поддержкой нескольких мониторов.
 *
 * Сборка:        make
 * Установка:     make install
 * Информация:    lindu-wm -v
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/Xinerama.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>

#define LENGTH(X)            (sizeof(X) / sizeof(X[0]))
#define MAX(x, y)            (((x) > (y)) ? (x) : (y))
#define MIN(x, y)            (((x) < (y)) ? (x) : (y))
#define CLEANMASK(mask)      (mask & ~(numlockmask | LockMask))

#define NTAGS  9
#define T1 0
#define T2 1
#define T3 2
#define T4 3
#define T5 4
#define T6 5
#define T7 6
#define T8 7
#define T9 8
#define TLAST NTAGS

#define TAGMASK (((unsigned int)-1) >> (32 - NTAGS))

enum { LT_TILE, LT_MONOCLE, LT_FLOAT, LT_LAST };

enum {
    NetSupported, NetWMName, NetActiveWindow,
    NetWMState, NetWMFullscreen,
    NetWMWindowType, NetWMWindowTypeDialog,
    NetWMWindowTypeDock, NetWMWindowTypeDesktop, NetWMWindowTypeUtility,
    NetLast
};
static Atom atoms[NetLast];

static Display *dpy;
static int screen;
static int scrw, scrh;
static int barvisible = 1;
static int barh;
static int numlockmask = 0;
static XFontStruct *barfont;
static GC bargc;
static unsigned long col_inact;
static unsigned long col_bar, col_baract, col_barline, col_bartxt;
static int running = 1;
static time_t laststatus = 0;
static Atom wm_delete, wm_protocols;

typedef struct Client Client;
struct Client {
    Client *next;
    Client *snext;
    Window win;
    struct Monitor *mon;
    int x, y, w, h;
    int ox, oy, ow, oh;
    int isfloating;
    int isfullscreen;
    int isfixed;
    unsigned int tags;
};

typedef struct Monitor Monitor;
struct Monitor {
    Monitor *next;
    Client *sel;
    unsigned int curtag;
    int curlayout;
    int x, y, w, h;
    int num;
    Window barwin;
};

static Monitor *monitors;
static Monitor *m;              /* активный монитор */
static Client *clients;
static Client *stack;

/* аргументы действий и таблица клавиш */
typedef union {
    int i;
    unsigned int ui;
    const void *v;
} Arg;

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

/* ---- прототипы ---- */
static void die(const char *msg);
static unsigned long getcolor(const char *name);
static Client *addtoclient(Window w);
static void removeclient(Client *c);
static void focus(Client *c);
static void unfocus(Client *gone);
static void drawbar(Monitor *mon);
static void arrange(void);
static void resize(Client *c, int x, int y, int w, int h);
static void applysize(Client *c, XWindowChanges *wc, int n);
static void grabbuttons(Client *c);
static void grabkeys(void);
static void setup(void);
static void run(void);
static void view(const Arg *arg);
static void toggletag(const Arg *arg);
static void tagclient(const Arg *arg);
static void spawn(const Arg *arg);
static void quit(const Arg *arg);
static void killclient(const Arg *arg);
static void togglefloat(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void setlayout(const Arg *arg);
static void togglebar(const Arg *arg);
static void focusnext(const Arg *arg);
static void focusprev(const Arg *arg);
static void movestack(const Arg *arg);
static void resizemaster(const Arg *arg);
static void incrementx(const Arg *arg);
static void focusmon(const Arg *arg);
static void tagmon(const Arg *arg);

/* ---------------- конфигурация (горячие клавиши, цвета) --------------- */
#include "config.h"

/* --- аргументы для кнопок встроенной панели --- */
static const Arg arg_rofi = SHCMD("rofi -show drun");
static const Arg arg_inst = SHCMD("/usr/local/bin/lindu-install-gtk");

/* ----------------------------- утилиты ------------------------------ */

static void
die(const char *msg)
{
    fprintf(stderr, "lindu-wm: %s\n", msg);
    exit(1);
}

static unsigned long
getcolor(const char *name)
{
    XColor c;
    Colormap cmap = DefaultColormap(dpy, screen);
    if (!XAllocNamedColor(dpy, cmap, name, &c, &c))
        return BlackPixel(dpy, screen);
    return c.pixel;
}

/* --------------------------- мониторы -------------------------------- */

static Monitor *
createmonitor(int x, int y, int w, int h, int num)
{
    Monitor *mon;

    if (!(mon = calloc(1, sizeof(Monitor))))
        die("не хватает памяти");
    mon->x = x; mon->y = y; mon->w = w; mon->h = h;
    mon->num = num;
    mon->curtag = 1;                 /* первый тег */
    mon->curlayout = LT_TILE;

    mon->barwin = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                                      x, y + h - barh, w, barh, 0,
                                      col_inact, col_inact);
    XSelectInput(dpy, mon->barwin, ExposureMask | ButtonPressMask);
    XStoreName(dpy, mon->barwin, "lindu-bar");

    mon->next = monitors;
    monitors = mon;
    return mon;
}

static Monitor *
recttomon(int xx, int yy)
{
    Monitor *mon, *best = monitors;
    int dist, bestdist = INT_MAX;

    for (mon = monitors; mon; mon = mon->next)
        if (xx >= mon->x && xx < mon->x + mon->w &&
            yy >= mon->y && yy < mon->y + mon->h)
            return mon;
    /* точка вне всех экранов (зазор) — берём ближайший */
    for (mon = monitors; mon; mon = mon->next) {
        dist = (xx - (mon->x + mon->w / 2)) * (xx - (mon->x + mon->w / 2)) +
               (yy - (mon->y + mon->h / 2)) * (yy - (mon->y + mon->h / 2));
        if (dist < bestdist) {
            bestdist = dist;
            best = mon;
        }
    }
    return best;
}

static Monitor *
prevmon(Monitor *from)
{
    Monitor *last = NULL, *mm;

    for (mm = monitors; mm; mm = mm->next) {
        if (mm->next == from)
            return mm;
        last = mm;
    }
    return last;   /* wrap: from — первый, возврат последнего */
}

static void
setmon(Client *c, Monitor *mon)
{
    c->mon = mon;
    c->tags = mon->curtag;
}

/* --------------------------- работа с окнами -------------------------- */

static Client *
addtoclient(Window w)
{
    Client *c;
    XWindowAttributes wa;

    if (!(c = calloc(1, sizeof(Client))))
        die("не хватает памяти");
    c->win = w;
    c->tags = m ? m->curtag : 1;
    if (!XGetWindowAttributes(dpy, w, &wa))
        die("не удалось получить атрибуты окна");
    c->x = wa.x; c->y = wa.y; c->w = wa.width; c->h = wa.height;
    c->ox = wa.x; c->oy = wa.y; c->ow = wa.width; c->oh = wa.height;
    c->mon = recttomon(c->x + c->w / 2, c->y + c->h / 2);

    /* панели (DOCK/DESKTOP/UTILITY/...) не тайлим — им нужен плавающий режим */
    {
        Atom type;
        int fmt;
        unsigned long n, after;
        unsigned char *prop = NULL;
        if (XGetWindowProperty(dpy, w, atoms[NetWMWindowType], 0, 1,
                               False, XA_ATOM, &type, &fmt, &n, &after,
                               &prop) == Success && prop) {
            Atom t = ((Atom *)prop)[0];
            XFree(prop);
            if (t == atoms[NetWMWindowTypeDialog] ||
                t == atoms[NetWMWindowTypeDock] ||
                t == atoms[NetWMWindowTypeDesktop] ||
                t == atoms[NetWMWindowTypeUtility])
                c->isfloating = 1;
        }
    }

    c->snext = stack;
    stack = c;
    c->next = clients;
    clients = c;
    return c;
}

static void
removeclient(Client *c)
{
    Client **cp;

    for (cp = &clients; *cp; cp = &(*cp)->next)
        if (*cp == c)
            break;
    if (*cp)
        *cp = c->next;

    for (cp = &stack; *cp; cp = &(*cp)->snext)
        if (*cp == c)
            break;
    if (*cp)
        *cp = c->snext;

    free(c);
}

/* ------------------------------- фокус ------------------------------- */

static void
focus(Client *c)
{
    Client *old;

    if (c) {
        old = m ? m->sel : NULL;
        m = c->mon;
        m->sel = c;
        XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
        XRaiseWindow(dpy, c->win);
        XChangeProperty(dpy, DefaultRootWindow(dpy), atoms[NetActiveWindow],
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *)&(c->win), 1);
        if (old != c)
            drawbar(m);
    } else if (m) {
        m->sel = NULL;
        XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, DefaultRootWindow(dpy), atoms[NetActiveWindow]);
        drawbar(m);
    }
}

static void
focusnext(const Arg *arg)
{
    Client *c;
    Monitor *mon = m ? m : monitors;
    (void)arg;

    for (c = mon->sel ? mon->sel->next : clients; c; c = c->next)
        if (c->mon == mon && (c->tags & mon->curtag)) {
            focus(c);
            return;
        }
    for (c = clients; c; c = c->next)
        if (c->mon == mon && (c->tags & mon->curtag)) {
            focus(c);
            return;
        }
}

static void
focusprev(const Arg *arg)
{
    Client *c, *prev = NULL;
    Monitor *mon = m ? m : monitors;
    (void)arg;

    for (c = clients; c && c != mon->sel; c = c->next)
        if (c->mon == mon && (c->tags & mon->curtag))
            prev = c;
    if (prev)
        focus(prev);
    else if (c && (c->tags & mon->curtag) && c->mon == mon)
        focus(c);
}

/* восстановить фокус на всех мониторах после закрытия окна */
static void
unfocus(Client *gone)
{
    Monitor *mon;
    Client *cc, *nextsel;

    for (mon = monitors; mon; mon = mon->next) {
        if (mon->sel != gone)
            continue;
        mon->sel = NULL;
        nextsel = NULL;
        for (cc = clients; cc; cc = cc->next)
            if (cc->mon == mon && (cc->tags & mon->curtag)) {
                nextsel = cc;
                break;
            }
        if (mon == m) {
            if (nextsel)
                focus(nextsel);
            else
                focus(NULL);
        } else {
            mon->sel = nextsel;
        }
    }
}

/* ----------------------------- геометрия ------------------------------ */

static void
resize(Client *c, int x, int y, int w, int h)
{
    XWindowChanges wc;

    c->x = x; c->y = y; c->w = w; c->h = h;
    wc.x = x; wc.y = y; wc.width = w; wc.height = h;
    XConfigureWindow(dpy, c->win, CWX | CWY | CWWidth | CWHeight, &wc);
}

static void
applysize(Client *c, XWindowChanges *wc, int n)
{
    if (n & CWX) c->x = wc->x;
    if (n & CWY) c->y = wc->y;
    if (n & CWWidth) c->w = wc->width;
    if (n & CWHeight) c->h = wc->height;
    XConfigureWindow(dpy, c->win, n, wc);
}

/* ------------------------------- раскладки ---------------------------- */

static int
counttiled(Monitor *mon)
{
    Client *c;
    int n = 0;

    for (c = clients; c; c = c->next)
        if (c->mon == mon && (c->tags & mon->curtag) &&
            !c->isfloating && !c->isfullscreen)
            n++;
    return n;
}

static void
tile(Monitor *mon)
{
    Client *c;
    int n, x, y, w, h, mh;
    int i = 0;

    n = counttiled(mon);
    if (!n)
        return;

    x = mon->x;
    w = mon->w;
    h = mon->h - (barvisible ? barh : 0);
    y = mon->y;
    mh = (n == 1) ? h : h / (n - 1);

    for (c = clients; c; c = c->next) {
        if (c->mon != mon || !(c->tags & mon->curtag))
            continue;
        if (c->isfullscreen) {
            resize(c, mon->x, mon->y, mon->w, mon->h);
            continue;
        }
        if (c->isfloating) {
            resize(c, c->ox, c->oy, c->ow, c->oh);
            continue;
        }
        if (i == 0)
            resize(c, x, y, w * MASTERFACTOR / 100, h);
        else
            resize(c, x + w * MASTERFACTOR / 100, y + (i - 1) * mh,
                   w - w * MASTERFACTOR / 100, mh);
        i++;
    }
}

static void
monocle(Monitor *mon)
{
    Client *c;
    int y = mon->y;
    int h = mon->h - (barvisible ? barh : 0);

    for (c = clients; c; c = c->next) {
        if (c->mon != mon || !(c->tags & mon->curtag))
            continue;
        if (c->isfullscreen) {
            resize(c, mon->x, mon->y, mon->w, mon->h);
            continue;
        }
        if (c->isfloating) {
            resize(c, c->ox, c->oy, c->ow, c->oh);
            continue;
        }
        resize(c, mon->x, y, mon->w, h);
    }
}

static void
floating(Monitor *mon)
{
    Client *c;

    for (c = clients; c; c = c->next)
        if (c->mon == mon && (c->tags & mon->curtag) && !c->isfullscreen)
            resize(c, c->ox, c->oy, c->ow, c->oh);
}

static void
arrange(void)
{
    Monitor *mon;

    for (mon = monitors; mon; mon = mon->next) {
        switch (mon->curlayout) {
        case LT_TILE:    tile(mon);    break;
        case LT_MONOCLE: monocle(mon); break;
        case LT_FLOAT:   floating(mon); break;
        }
        drawbar(mon);
    }
}

/* ------------------------------ панель -------------------------------- */

/* сколько видимых окон на мониторе в текущем теге */
static int
barcount(Monitor *mon)
{
    Client *c;
    int n = 0;

    for (c = clients; c; c = c->next)
        if (c->mon == mon && (c->tags & mon->curtag) && !c->isfullscreen)
            n++;
    return n;
}

/* заголовок окна, только печатные ASCII символы (шрифт панели — Latin-1) */
static const char *
bartitle(Client *c)
{
    static char buf[160];
    char *name = NULL;
    size_t n = 0;
    unsigned int i;

    buf[0] = '\0';
    if (XFetchName(dpy, c->win, &name) && name) {
        for (i = 0; name[i] && n < sizeof(buf) - 1; i++) {
            unsigned char ch = (unsigned char)name[i];
            buf[n++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
        }
        buf[n] = '\0';
        XFree(name);
        return buf;
    }
    return "app";
}

/* иконка кнопки: kind 1 — сетка 2x2 (как у Windows), kind 2 — стрелка вниз */
static void
baricon(Monitor *mon, int cx, int cy, int kind)
{
    int s = 6, g = 2;

    XSetForeground(dpy, bargc, col_bartxt);
    if (kind == 1) {
        int x0 = cx - (2 * s + g) / 2;
        int y0 = cy - (2 * s + g) / 2;
        XFillRectangle(dpy, mon->barwin, bargc, x0, y0, s, s);
        XFillRectangle(dpy, mon->barwin, bargc, x0 + s + g, y0, s, s);
        XFillRectangle(dpy, mon->barwin, bargc, x0, y0 + s + g, s, s);
        XFillRectangle(dpy, mon->barwin, bargc, x0 + s + g, y0 + s + g, s, s);
    } else {
        XFillRectangle(dpy, mon->barwin, bargc, cx - 2, cy - 9, 4, 11);
        XFillRectangle(dpy, mon->barwin, bargc, cx - 7, cy + 2, 14, 4);
    }
}

/* кнопка панели: подложка + иконка по центру */
static void
barbutton(Monitor *mon, int x0, int x1, int active, int kind)
{
    int w = x1 - x0;
    int cx = x0 + w / 2;

    if (active) {
        XSetForeground(dpy, bargc, col_barline);
        XFillRectangle(dpy, mon->barwin, bargc, x0, 0, w, barh);
        XSetForeground(dpy, bargc, col_baract);
        XFillRectangle(dpy, mon->barwin, bargc, x0 + 1, 1, w - 2, barh - 2);
    }
    baricon(mon, cx, barh / 2, kind);
}

/* список открытых окон (задачи) */
static void
bartasks(Monitor *mon)
{
    Client *c;
    int n = barcount(mon);
    int x0 = START_W + INST_W;
    int x1 = mon->w - CLOCK_W;
    int tw, i = 0;

    if (!n)
        return;
    tw = (x1 - x0) / n;
    if (tw < 50)
        tw = 50;
    for (c = clients; c; c = c->next) {
        const char *tt;
        int maxw, tl;
        if (c->mon != mon || !(c->tags & mon->curtag) || c->isfullscreen)
            continue;
        XSetForeground(dpy, bargc, col_baract);
        XFillRectangle(dpy, mon->barwin, bargc, x0 + i * tw + 2, 2, tw - 4, barh - 4);
        if (c == mon->sel) {
            XSetForeground(dpy, bargc, col_barline);
            XFillRectangle(dpy, mon->barwin, bargc, x0 + i * tw + 2, 2, tw - 4, 2);
        }
        tt = bartitle(c);
        tl = (int)strlen(tt);
        maxw = tw - 10;
        while (tl > 0 && XTextWidth(barfont, tt, tl) > maxw)
            tl--;
        XSetForeground(dpy, bargc, col_bartxt);
        XDrawString(dpy, mon->barwin, bargc, x0 + i * tw + 5, barh - PADDING_Y,
                    tt, tl);
        i++;
    }
}

/* часы справа */
static void
barclock(Monitor *mon)
{
    char buf[64];
    time_t tmnow;
    struct tm *tm;
    int w;

    tmnow = time(NULL);
    tm = localtime(&tmnow);
    snprintf(buf, sizeof(buf), "%02d:%02d %02d.%02d",
             tm->tm_hour, tm->tm_min, tm->tm_mday, tm->tm_mon + 1);
    w = XTextWidth(barfont, buf, (int)strlen(buf));
    XSetForeground(dpy, bargc, col_bartxt);
    XDrawString(dpy, mon->barwin, bargc, mon->w - w - PADDING_X, barh - PADDING_Y,
                buf, (int)strlen(buf));
}

static void
drawbar(Monitor *mon)
{
    if (!barvisible)
        return;

    XSetForeground(dpy, bargc, col_bar);
    XFillRectangle(dpy, mon->barwin, bargc, 0, 0, mon->w, barh);
    barbutton(mon, 0, START_W, 0, 1);              /* Пуск → меню приложений */
    barbutton(mon, START_W, START_W + INST_W, 0, 2); /* установщик          */
    bartasks(mon);
    barclock(mon);
    XFlush(dpy);
}

/* клик по панели: Пуск / установщик / переключение задач */
static void
barclick(Monitor *mon, int x)
{
    Client *c;
    int n, x0, x1, tw, i = 0;

    if (x < START_W) {
        spawn(&arg_rofi);
        return;
    }
    if (x < START_W + INST_W) {
        spawn(&arg_inst);
        return;
    }
    n = barcount(mon);
    if (!n)
        return;
    x0 = START_W + INST_W;
    x1 = mon->w - CLOCK_W;
    tw = (x1 - x0) / n;
    if (tw < 50)
        tw = 50;
    for (c = clients; c; c = c->next) {
        if (c->mon != mon || !(c->tags & mon->curtag) || c->isfullscreen)
            continue;
        if (x >= x0 + i * tw && x < x0 + (i + 1) * tw) {
            focus(c);
            return;
        }
        i++;
    }
}

static void
drawbars(void)
{
    Monitor *mon;

    for (mon = monitors; mon; mon = mon->next)
        drawbar(mon);
    XFlush(dpy);
}

/* --------------------------- перемещение окна -------------------------- */

static void
movestack(const Arg *arg)
{
    Client *c, *p, *pprev = NULL, *n = NULL;
    Monitor *mon = m ? m : monitors;
    Client **pp;

    if (!mon->sel)
        return;

    if (arg->i > 0) {
        for (p = clients; p && p != mon->sel; p = p->next)
            if ((p->mon == mon) && (p->tags & mon->curtag))
                pprev = p;
        if (!pprev)
            return;
        pp = &clients;
        while (*pp && *pp != mon->sel)
            pp = &(*pp)->next;
        *pp = mon->sel->next;
        c = mon->sel;
        c->next = pprev->next;
        pprev->next = c;
    } else {
        for (p = mon->sel->next; p; p = p->next)
            if ((p->mon == mon) && (p->tags & mon->curtag)) {
                n = p;
                break;
            }
        if (!n)
            return;
        pp = &clients;
        while (*pp && *pp != mon->sel)
            pp = &(*pp)->next;
        *pp = mon->sel->next;
        for (pp = &clients; *pp && *pp != n; pp = &(*pp)->next)
            ;
        c = mon->sel;
        c->next = n;
        *pp = c;
    }
    arrange();
}

static void
togglefloat(const Arg *arg)
{
    Monitor *mon = m ? m : monitors;
    Client *c = mon->sel;
    int workh;
    (void)arg;

    if (!c)
        return;
    c->isfloating = !c->isfloating;
    if (c->isfloating) {
        workh = mon->h - (barvisible ? barh : 0);
        c->ox = mon->x + MAX((mon->w - c->ow) / 2, 0);
        c->oy = mon->y + MAX((workh - c->oh) / 2, 0);
        resize(c, c->ox, c->oy, c->ow, c->oh);
    } else {
        arrange();
    }
}

static void
togglefullscreen(const Arg *arg)
{
    Monitor *mon = m ? m : monitors;
    Client *c = mon->sel;
    XWindowChanges wc;
    (void)arg;

    if (!c)
        return;
    c->isfullscreen = !c->isfullscreen;
    if (c->isfullscreen) {
        XChangeProperty(dpy, c->win, atoms[NetWMState], XA_ATOM, 32,
                        PropModeReplace,
                        (unsigned char *)&atoms[NetWMFullscreen], 1);
        wc.x = mon->x; wc.y = mon->y;
        wc.width = mon->w; wc.height = mon->h;
        XConfigureWindow(dpy, c->win, CWX | CWY | CWWidth | CWHeight, &wc);
        XRaiseWindow(dpy, c->win);
    } else {
        XDeleteProperty(dpy, c->win, atoms[NetWMState]);
        arrange();
    }
}

static void
resizemaster(const Arg *arg)
{
    Monitor *mon = m ? m : monitors;
    Client *c = mon->sel;
    XWindowChanges wc;

    if (!c)
        return;
    wc.height = c->h + arg->i;
    applysize(c, &wc, CWHeight);
}

static void
incrementx(const Arg *arg)
{
    XWindowChanges wc;

    if (!m || !m->sel)
        return;
    wc.x = m->sel->x + arg->i;
    wc.width = m->sel->w - arg->i;
    applysize(m->sel, &wc, CWX | CWWidth);
}

/* ------------------------------- теги --------------------------------- */

static void
setlayout(const Arg *arg)
{
    Monitor *mon = m ? m : monitors;

    mon->curlayout = arg->ui % LT_LAST;
    arrange();
}

static void
view(const Arg *arg)
{
    Monitor *mon = m ? m : monitors;
    Client *c;

    mon->curtag = arg->ui & TAGMASK;
    if (mon->sel && !(mon->sel->tags & mon->curtag))
        mon->sel = NULL;
    for (c = clients; c; c = c->next)
        if (c->mon == mon && (c->tags & mon->curtag)) {
            focus(c);
            break;
        }
    arrange();
}

static void
toggletag(const Arg *arg)
{
    Monitor *mon = m ? m : monitors;
    Client *c = mon->sel;

    if (!c)
        return;
    c->tags ^= arg->ui;
    if (!(c->tags & mon->curtag))
        mon->sel = NULL;
    arrange();
}

static void
tagclient(const Arg *arg)
{
    Monitor *mon = m ? m : monitors;
    Client *c = mon->sel;

    if (!c)
        return;
    c->tags = arg->ui;
    mon->sel = NULL;
    arrange();
}

/* --------------------------- навигация по мониторам -------------------- */

static void
focusmon(const Arg *arg)
{
    Monitor *nmon;

    if (!monitors)
        return;
    if (arg->i > 0)
        nmon = m ? (m->next ? m->next : monitors) : monitors;
    else
        nmon = m ? prevmon(m) : monitors;

    if (nmon && nmon != m) {
        m = nmon;
        focus(nmon->sel);
    }
}

static void
tagmon(const Arg *arg)
{
    Client *c;
    Monitor *nmon;

    if (!m || !m->sel)
        return;
    if (arg->i > 0)
        nmon = m->next ? m->next : monitors;
    else
        nmon = prevmon(m);
    if (!nmon || nmon == m)
        return;
    c = m->sel;
    m = nmon;
    c->mon = nmon;
    c->tags = nmon->curtag;
    focus(c);
    arrange();
}

/* --------------------------- прочее ----------------------------------- */

static void
spawn(const Arg *arg)
{
    if (fork() == 0) {
        setsid();
        if (dpy)
            close(ConnectionNumber(dpy));
        execl("/bin/sh", "sh", "-c", (char *)arg->v, (char *)NULL);
        _exit(1);
    }
}

static void
killclient(const Arg *arg)
{
    Monitor *mon = m ? m : monitors;
    Client *c = mon->sel;
    XEvent ev;
    (void)arg;

    if (!c)
        return;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = c->win;
    ev.xclient.message_type = wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, c->win, False, NoEventMask, &ev);
}

static void
quit(const Arg *arg)
{
    (void)arg;
    running = 0;
}

static void
togglebar(const Arg *arg)
{
    Monitor *mon;
    (void)arg;

    barvisible = !barvisible;
    for (mon = monitors; mon; mon = mon->next) {
        if (barvisible)
            XMapRaised(dpy, mon->barwin);
        else
            XUnmapWindow(dpy, mon->barwin);
    }
    arrange();
}

/* -------------------------- обработчики X ----------------------------- */

static void
grabbuttons(Client *c)
{
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    XGrabButton(dpy, Button1, MODKEY, c->win, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button3, MODKEY, c->win, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
}

static void
grabkeys(void)
{
    KeyCode code;
    unsigned int i;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask | LockMask };

    XUngrabKey(dpy, AnyKey, AnyModifier, DefaultRootWindow(dpy));
    for (i = 0; i < LENGTH(keys); i++) {
        code = XKeysymToKeycode(dpy, keys[i].keysym);
        if (!code)
            continue;
        unsigned int j;
        for (j = 0; j < LENGTH(modifiers); j++)
            XGrabKey(dpy, code, keys[i].mod | modifiers[j],
                     DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync);
    }
}

static void
maprequest(XEvent *e)
{
    XMapRequestEvent *ev = &e->xmaprequest;
    XWindowAttributes wa;
    Client *c;

    if (XGetWindowAttributes(dpy, ev->window, &wa) &&
        wa.override_redirect) {
        XMapWindow(dpy, ev->window);
        return;
    }

    for (c = clients; c; c = c->next)
        if (c->win == ev->window)
            break;
    if (!c)
        c = addtoclient(ev->window);
    grabbuttons(c);
    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    XMapWindow(dpy, c->win);
    focus(c);
    arrange();
}

static void
buttonpress(XEvent *e)
{
    Client *c;
    Monitor *bmon;
    XButtonEvent *be = &e->xbutton;

    /* клик по панели? */
    for (bmon = monitors; bmon; bmon = bmon->next)
        if (bmon->barwin == be->window) {
            barclick(bmon, be->x);
            return;
        }

    for (c = clients; c; c = c->next)
        if (c->win == be->window)
            break;
    if (!c)
        return;
    focus(c);

    if (be->button == Button1 && CLEANMASK(be->state) == MODKEY) {
        while (1) {
            XEvent te;
            XMaskEvent(dpy, PointerMotionMask, &te);
            if (te.type == ButtonRelease)
                break;
            c->x = te.xmotion.x_root - (c->w / 2);
            c->y = te.xmotion.y_root - (c->h / 2);
            c->ox = c->x; c->oy = c->y;
            XMoveWindow(dpy, c->win, c->x, c->y);
        }
    } else if (be->button == Button3 && CLEANMASK(be->state) == MODKEY) {
        int sw = c->w, sh = c->h;
        while (1) {
            XEvent te;
            int dx, dy;
            XMaskEvent(dpy, PointerMotionMask, &te);
            if (te.type == ButtonRelease)
                break;
            dx = te.xmotion.x_root - be->x_root;
            dy = te.xmotion.y_root - be->y_root;
            resize(c, c->x, c->y, MAX(sw + dx, MINW), MAX(sh + dy, MINH));
        }
        c->ow = c->w; c->oh = c->h;
    }
}

static void
unmapnotify(XEvent *e)
{
    XUnmapEvent *ev = &e->xunmap;
    Client **cp, *c;

    if (ev->send_event) {
        for (cp = &clients; *cp; cp = &(*cp)->next)
            if ((*cp)->win == ev->window)
                break;
        if (*cp) {
            c = *cp;
            removeclient(c);
            unfocus(c);
            arrange();
        }
    }
}

static void
destroynotify(XEvent *e)
{
    Client *c;
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    for (c = clients; c; c = c->next)
        if (c->win == ev->window)
            break;
    if (c) {
        removeclient(c);
        unfocus(c);
        arrange();
    }
}

static void
configurerequest(XEvent *e)
{
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    Client *c;
    XWindowChanges wc;

    for (c = clients; c; c = c->next)
        if (c->win == ev->window)
            break;
    if (!c)
        return;
    if (!(c->isfloating || c->isfullscreen))
        return;

    wc.x = MAX(ev->x, 0);
    wc.y = MAX(ev->y, 0);
    wc.width = MAX(ev->width, MINW);
    wc.height = MAX(ev->height, MINH);
    applysize(c, &wc, CWX | CWY | CWWidth | CWHeight);
    c->ox = wc.x; c->oy = wc.y; c->ow = wc.width; c->oh = wc.height;
}

static void
keypress(XEvent *e)
{
    unsigned int i;
    KeySym keysym;
    XKeyEvent *ke = &e->xkey;

    keysym = XkbKeycodeToKeysym(dpy, ke->keycode, 0, 0);
    for (i = 0; i < LENGTH(keys); i++) {
        if (keysym == keys[i].keysym &&
            CLEANMASK(keys[i].mod) == CLEANMASK(ke->state)) {
            keys[i].func(&keys[i].arg);
            return;
        }
    }
}

static void
enternotify(XEvent *e)
{
    Client *c;
    XCrossingEvent *ce = &e->xcrossing;
    Window focuswin;
    int revert = RevertToParent;

    if (ce->mode != NotifyNormal || ce->detail == NotifyInferior)
        return;
    for (c = clients; c; c = c->next)
        if (c->win == ce->window)
            break;
    if (!c)
        return;
    XGetInputFocus(dpy, &focuswin, &revert);
    if (ce->window != focuswin)
        focus(c);
}

static void
run(void)
{
    XEvent ev;

    while (running) {
        if (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            switch (ev.type) {
            case ConfigureRequest: configurerequest(&ev); break;
            case MapRequest:        maprequest(&ev);       break;
            case ButtonPress:       buttonpress(&ev);      break;
            case UnmapNotify:       unmapnotify(&ev);      break;
            case DestroyNotify:     destroynotify(&ev);    break;
            case KeyPress:          keypress(&ev);         break;
            case EnterNotify:       enternotify(&ev);      break;
            case Expose:            drawbars();            break;
            }
        } else if (barvisible && time(NULL) - laststatus >= 1) {
            laststatus = time(NULL);
            drawbars();
        } else {
            usleep(10000);
        }
    }
}

/* ------------------------------ setup --------------------------------- */

static void
setup(void)
{
    XModifierKeymap *modmap;
    Atom supported[NetLast];
    unsigned int i;

    screen = DefaultScreen(dpy);
    scrw = DisplayWidth(dpy, screen);
    scrh = DisplayHeight(dpy, screen);

    modmap = XGetModifierMapping(dpy);
    for (i = 0; i < 8; i++)
        if (modmap->modifiermap[i * modmap->max_keypermod] ==
            XKeysymToKeycode(dpy, XK_Num_Lock))
            numlockmask |= (1 << i);
    XFreeModifiermap(modmap);

    atoms[NetSupported]          = XInternAtom(dpy, "_NET_SUPPORTED", False);
    atoms[NetWMName]             = XInternAtom(dpy, "_NET_WM_NAME", False);
    atoms[NetActiveWindow]       = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    atoms[NetWMState]            = XInternAtom(dpy, "_NET_WM_STATE", False);
    atoms[NetWMFullscreen]       = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    atoms[NetWMWindowType]       = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    atoms[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    atoms[NetWMWindowTypeDock]   = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atoms[NetWMWindowTypeDesktop]= XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    atoms[NetWMWindowTypeUtility]= XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    wm_protocols  = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete     = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    for (i = 0; i < NetLast; i++)
        supported[i] = atoms[i];
    XChangeProperty(dpy, DefaultRootWindow(dpy), atoms[NetSupported],
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)supported, NetLast);

    barh = BARH;
    if ((barfont = XLoadQueryFont(dpy, FONT)) == NULL)
        barfont = XLoadQueryFont(dpy, "fixed");
    if (!barfont)
        die("не найден шрифт для панели (установите terminus-font)");
    col_inact  = getcolor(INACTIVE);
    col_bar    = getcolor(ACTIVE);
    col_baract = getcolor(BARACT);
    col_barline= getcolor(BARLINE);
    col_bartxt = getcolor(ACCENT);
    bargc = XCreateGC(dpy, DefaultRootWindow(dpy), 0, NULL);
    XSetFont(dpy, bargc, barfont->fid);

    /* обнаружение мониторов через Xinerama */
    {
        int n = 0;
        XineramaScreenInfo *info;

        if (XineramaIsActive(dpy)) {
            info = XineramaQueryScreens(dpy, &n);
            if (info && n > 0) {
                for (i = 0; i < (unsigned int)n; i++)
                    createmonitor(info[i].x_org, info[i].y_org,
                                  info[i].width, info[i].height, i);
                XFree(info);
            }
        }
        if (!monitors)
            createmonitor(0, 0, scrw, scrh, 0);
    }

    XSelectInput(dpy, DefaultRootWindow(dpy),
                 SubstructureRedirectMask | SubstructureNotifyMask |
                 ButtonPressMask | EnterWindowMask | KeyPressMask);
    grabkeys();

    for (m = monitors; m; m = m->next)
        if (barvisible)
            XMapRaised(dpy, m->barwin);
    /* активным делаем первый (главный) монитор */
    m = NULL;
    {
        Monitor *mm;
        for (mm = monitors; mm; mm = mm->next)
            if (mm->num == 0) {
                m = mm;
                break;
            }
        if (!m)
            m = monitors;
    }
    XStoreName(dpy, DefaultRootWindow(dpy), TITLE);
    XSync(dpy, False);
    laststatus = time(NULL);
}

int
main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        printf("lindu-wm " VERSION " — оконный менеджер lindu linux\n");
        return 0;
    }

    signal(SIGCHLD, SIG_IGN);
    if (!(dpy = XOpenDisplay(NULL)))
        die("не удалось открыть дисплей X");

    setup();
    run();

    XCloseDisplay(dpy);
    return 0;
}
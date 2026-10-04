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
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <sys/time.h>

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
    NetWMStateMaxH, NetWMStateMaxV, NetWMStateHidden,
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
static XftFont *barxft;   /* шрифт панели (Xft, масштабируемый) */
static GC bargc;
static unsigned long col_inact;
static unsigned long col_bar, col_baract, col_barline, col_bartxt;
static int running = 1;
static time_t laststatus = 0;
static Atom wm_delete, wm_protocols, wm_state, wm_change_state;
static Cursor cursor;

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
    int ishidden;         /* окно свёрнуто, но живо */
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
static void drawbars(void);
static void arrange(void);
static void clientstate(Client *c, int state);
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
/* имя скрипта Wi-Fi (склейка литералов) */
static const Arg arg_wifi = SHCMD("/usr/local/bin/lind""u-wifi");
static const Arg arg_setup = SHCMD("/usr/local/bin/lind""u-settings");

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

/* ------------------------- настройки и темы ---------------------------- */
/* Всё живёт в ~/.config/lindu/theme.conf, файл перечитывается на лету:
 * тема и параметры применяются без перезапуска менеджера. */

static int   opt_barh     = BARH;
static int   opt_dragtop  = DRAG_TOP;
static int   opt_clock    = 1;        /* 0 = ЧЧ:ММ, 1 = ЧЧ:ММ ДД.ММ, 2 = секунды */
static int   opt_master   = MASTERFACTOR;
static int   opt_bar      = 1;
static char  opt_theme[32] = "dark";

static char  cfg_bar[64]  = ACTIVE;
static char  cfg_text[64] = ACCENT;
static char  cfg_act[64]  = BARACT;
static char  cfg_line[64] = BARLINE;

static time_t cfgmtime = 0;

static void
applycolors(void)
{
    col_inact   = getcolor(cfg_bar);
    col_bar     = col_inact;
    col_bartxt  = getcolor(cfg_text);
    col_baract  = getcolor(cfg_act);
    col_barline = getcolor(cfg_line);
}

/* применить размеры и видимость панели */
static void
applybar(void)
{
    Monitor *mon;

    barh = barvisible ? opt_barh : 0;
    for (mon = monitors; mon; mon = mon->next) {
        if (barh > 0) {
            XMoveResizeWindow(dpy, mon->barwin, mon->x, mon->y + mon->h - barh,
                              mon->w, barh);
            XMapWindow(dpy, mon->barwin);
        } else {
            XUnmapWindow(dpy, mon->barwin);
        }
    }
    barvisible = barh > 0;
    XFlush(dpy);
}

static void
loadconfig(void)
{
    char path[512], ln[256], key[64], val[64];
    const char *home = getenv("HOME");
    FILE *f;
    char *p;

    if (!home)
        return;
    snprintf(path, sizeof(path), "%s/.config/lindu/theme.conf", home);
    f = fopen(path, "r");
    if (f) {
        while (fgets(ln, sizeof(ln), f)) {
            if (sscanf(ln, "%63s = %63s", key, val) != 2)
                continue;
            for (p = val; *p; p++)
                if (*p == '"' || *p == '\'')
                    *p = '\0';
            if (!strcmp(key, "bar"))
                snprintf(cfg_bar, sizeof(cfg_bar), "%s", val);
            else if (!strcmp(key, "text"))
                snprintf(cfg_text, sizeof(cfg_text), "%s", val);
            else if (!strcmp(key, "act"))
                snprintf(cfg_act, sizeof(cfg_act), "%s", val);
            else if (!strcmp(key, "line"))
                snprintf(cfg_line, sizeof(cfg_line), "%s", val);
            else if (!strcmp(key, "barh"))
                opt_barh = atoi(val);
            else if (!strcmp(key, "dragtop"))
                opt_dragtop = atoi(val);
            else if (!strcmp(key, "clock"))
                opt_clock = atoi(val);
            else if (!strcmp(key, "master"))
                opt_master = atoi(val);
            else if (!strcmp(key, "showbar"))
                opt_bar = atoi(val);
            else if (!strcmp(key, "name"))
                snprintf(opt_theme, sizeof(opt_theme), "%s", val);
        }
        fclose(f);
    }
    if (opt_barh < 26 || opt_barh > 64)
        opt_barh = BARH;
    if (opt_dragtop < 30 || opt_dragtop > 320)
        opt_dragtop = DRAG_TOP;
    if (opt_master < 35 || opt_master > 75)
        opt_master = MASTERFACTOR;
    if (opt_clock < 0 || opt_clock > 2)
        opt_clock = 1;
    opt_bar = opt_bar ? 1 : 0;
    applycolors();
}

/* следить за файлом настроек и применять изменения на лету */
static void
checkconfig(void)
{
    char path[512];
    const char *home = getenv("HOME");
    struct stat st;

    if (!home)
        return;
    snprintf(path, sizeof(path), "%s/.config/lindu/theme.conf", home);
    if (stat(path, &st) != 0)
        return;
    if ((time_t)st.st_mtime == cfgmtime)
        return;
    cfgmtime = (time_t)st.st_mtime;
    loadconfig();
    applybar();
    arrange();
    drawbars();
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
            !c->isfloating && !c->isfullscreen && !c->ishidden)
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
        if (c->ishidden)
            continue;
        if (i == 0)
            resize(c, x, y, w * opt_master / 100, h);
        else
            resize(c, x + w * opt_master / 100, y + (i - 1) * mh,
                   w - w * opt_master / 100, mh);
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
        if (c->ishidden)
            continue;
        resize(c, mon->x, y, mon->w, h);
    }
}

static void
floating(Monitor *mon)
{
    Client *c;

    for (c = clients; c; c = c->next)
        if (c->mon == mon && (c->tags & mon->curtag) && !c->isfullscreen &&
            !c->ishidden)
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
    int s = 7, g = 2, r;
    XGCValues gcv;

    /* толстые линии нужны значкам Wi-Fi, крестика и установщика */
    gcv.line_width = 2;
    gcv.cap_style = CapRound;
    gcv.join_style = JoinRound;

    XSetForeground(dpy, bargc, col_bartxt);
    if (kind == 1) {
        /* Пуск: логотип-сетка из четырёх плиток, одна выделена */
        int x0 = cx - (2 * s + g) / 2;
        int y0 = cy - (2 * s + g) / 2;
        XFillRectangle(dpy, mon->barwin, bargc, x0, y0, s, s);
        XFillRectangle(dpy, mon->barwin, bargc, x0 + s + g, y0, s, s);
        XFillRectangle(dpy, mon->barwin, bargc, x0, y0 + s + g, s, s);
        XSetForeground(dpy, bargc, col_barline);
        XFillRectangle(dpy, mon->barwin, bargc, x0 + s + g, y0 + s + g, s, s);
    } else if (kind == 2) {
        /* установщик: стрелка вниз в подставку */
        XChangeGC(dpy, bargc, GCLineWidth | GCCapStyle | GCJoinStyle, &gcv);
        XDrawLine(dpy, mon->barwin, bargc, cx, cy - 9, cx, cy + 1);
        XDrawLine(dpy, mon->barwin, bargc, cx - 4, cy - 3, cx, cy + 2);
        XDrawLine(dpy, mon->barwin, bargc, cx + 4, cy - 3, cx, cy + 2);
        XSetLineAttributes(dpy, bargc, 0, LineSolid, CapButt, JoinMiter);
        XFillRectangle(dpy, mon->barwin, bargc, cx - 9, cy + 5, 18, 3);
    } else if (kind == 3) {
        /* Wi-Fi: три дуги антенны и точка основания */
        XChangeGC(dpy, bargc, GCLineWidth | GCCapStyle | GCJoinStyle, &gcv);
        for (r = 6; r <= 14; r += 4)
            XDrawArc(dpy, mon->barwin, bargc, cx - r, cy + 9 - r, 2 * r, 2 * r,
                     40 * 64, 100 * 64);
        XSetLineAttributes(dpy, bargc, 0, LineSolid, CapButt, JoinMiter);
        XFillRectangle(dpy, mon->barwin, bargc, cx - 2, cy + 8, 4, 4);
    } else if (kind == 5) {
        /* шестерёнка: настройки системы */
        XChangeGC(dpy, bargc, GCLineWidth | GCCapStyle | GCJoinStyle, &gcv);
        XDrawArc(dpy, mon->barwin, bargc, cx - 6, cy - 6, 12, 12, 0, 360 * 64);
        XDrawArc(dpy, mon->barwin, bargc, cx - 2, cy - 2, 4, 4, 0, 360 * 64);
        XSetLineAttributes(dpy, bargc, 0, LineSolid, CapButt, JoinMiter);
        XFillRectangle(dpy, mon->barwin, bargc, cx - 1, cy - 10, 2, 4);
        XFillRectangle(dpy, mon->barwin, bargc, cx - 1, cy + 6, 2, 4);
        XFillRectangle(dpy, mon->barwin, bargc, cx - 10, cy - 1, 4, 2);
        XFillRectangle(dpy, mon->barwin, bargc, cx + 6, cy - 1, 4, 2);
        XFillRectangle(dpy, mon->barwin, bargc, cx - 8, cy - 8, 3, 3);
        XFillRectangle(dpy, mon->barwin, bargc, cx + 5, cy - 8, 3, 3);
        XFillRectangle(dpy, mon->barwin, bargc, cx - 8, cy + 5, 3, 3);
        XFillRectangle(dpy, mon->barwin, bargc, cx + 5, cy + 5, 3, 3);
    } else if (kind == 4) {
        /* крестик: закрыть активное окно */
        XChangeGC(dpy, bargc, GCLineWidth | GCCapStyle | GCJoinStyle, &gcv);
        XDrawLine(dpy, mon->barwin, bargc, cx - 6, cy - 6, cx + 6, cy + 6);
        XDrawLine(dpy, mon->barwin, bargc, cx + 6, cy - 6, cx - 6, cy + 6);
        XSetLineAttributes(dpy, bargc, 0, LineSolid, CapButt, JoinMiter);
    } else {
        /* запасной значок: стрелка вниз */
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
/* Буфер глифов для измерения ширины текста. */
#define BARGLYPHS 512
static XGlyphInfo barglyphs[BARGLYPHS];

/* Ширина текста панели в пикселях: Xft отдаёт по одному глифу на символ,
 * ширина строки — сумма смещений xOff. */
static int
bartextwidth(const char *s, int len)
{
    int i, total = 0;

    if (!barxft || len <= 0)
        return 0;
    if (len > BARGLYPHS)
        len = BARGLYPHS;
    XftTextExtents8(dpy, barxft, (const FcChar8 *)s, len, barglyphs);
    for (i = 0; i < len; i++)
        total += barglyphs[i].xOff;
    return total;
}

/* Цвет панели для Xft: XftColorAllocValue ждёт XRenderColor, поэтому нужный
 * оттенок сначала читаем из колоровой карты. */
static void
barxftcolor(unsigned long pixel, XftColor *out)
{
    XColor xc;
    XRenderColor rc;

    memset(&rc, 0, sizeof(rc));

    memset(&xc, 0, sizeof(xc));
    xc.pixel = pixel;
    xc.flags = DoRed | DoGreen | DoBlue;
    if (!XQueryColors(dpy, DefaultColormap(dpy, screen), &xc, 1))
        return;
    rc.red = xc.red;
    rc.green = xc.green;
    rc.blue = xc.blue;
    rc.alpha = 0xffff;
    XftColorAllocValue(dpy, DefaultVisual(dpy, screen),
                       DefaultColormap(dpy, screen), &rc, out);
}

/* Текст панели: Xft рисует через XftDraw, а не напрямую через окно. */
static void
bardrawtext(Monitor *mon, int x, int y, const char *s, int len)
{
    XftDraw *dr;
    XftColor color;

    if (!barxft || len <= 0)
        return;
    dr = XftDrawCreate(dpy, mon->barwin, DefaultVisual(dpy, screen),
                       DefaultColormap(dpy, screen));
    if (!dr)
        return;
    barxftcolor(col_bartxt, &color);
    XftDrawString8(dr, &color, barxft, x, y, (const FcChar8 *)s, len);
    XftDrawDestroy(dr);
}

static void
bartasks(Monitor *mon)
{
    Client *c;
    int n = barcount(mon);
    int x0 = START_W + INST_W + WIFI_W + SETTINGS_W + CLOSE_W;
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
        maxw = tw - 14;
        while (tl > 1 && bartextwidth(tt, tl) > maxw)
            tl--;
        bardrawtext(mon, x0 + i * tw + 7, barh - PADDING_Y, tt, tl);
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
    if (opt_clock == 0)
        snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
    else if (opt_clock == 2)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min,
                 tm->tm_sec);
    else
        snprintf(buf, sizeof(buf), "%02d:%02d %02d.%02d",
                 tm->tm_hour, tm->tm_min, tm->tm_mday, tm->tm_mon + 1);
    w = bartextwidth(buf, (int)strlen(buf));
    bardrawtext(mon, mon->w - w - PADDING_X, barh - PADDING_Y, buf, (int)strlen(buf));
}

static void
drawbar(Monitor *mon)
{
    if (!mon || !barvisible)
        return;

    XSetForeground(dpy, bargc, col_bar);
    XFillRectangle(dpy, mon->barwin, bargc, 0, 0, mon->w, barh);
    barbutton(mon, 0, START_W, 0, 1);              /* Пуск → меню приложений */
    barbutton(mon, START_W, START_W + INST_W, 0, 2); /* установщик          */
    barbutton(mon, START_W + INST_W, START_W + INST_W + WIFI_W, 0, 3); /* Wi-Fi */
    /* настройки */
    barbutton(mon, START_W + INST_W + WIFI_W,
              START_W + INST_W + WIFI_W + SETTINGS_W, 0, 5);
    /* закрыть активное окно */
    barbutton(mon, START_W + INST_W + WIFI_W + SETTINGS_W,
              START_W + INST_W + WIFI_W + SETTINGS_W + CLOSE_W, 0, 4);
    /* тонкая вертикальная черта: отделяет кнопки от списка задач */
    XSetForeground(dpy, bargc, col_barline);
    XFillRectangle(dpy, mon->barwin, bargc,
                   START_W + INST_W + WIFI_W + SETTINGS_W + CLOSE_W - 12, 6, 1, barh - 12);
    bartasks(mon);
    barclock(mon);
    XFlush(dpy);
}

/* клик по панели: Пуск / установщик / переключение задач */
/* двойной щелчок по кнопке задачи */
static Window lastbarwin = None;
static long lastbarms = 0;

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
    if (x < START_W + INST_W + WIFI_W) {
        spawn(&arg_wifi);
        return;
    }
    if (x < START_W + INST_W + WIFI_W + SETTINGS_W) {
        spawn(&arg_setup);
        return;
    }
    if (x < START_W + INST_W + WIFI_W + SETTINGS_W + CLOSE_W) {
        killclient(NULL);
        return;
    }
    n = barcount(mon);
    if (!n)
        return;
    x0 = START_W + INST_W + WIFI_W + SETTINGS_W + CLOSE_W;
    x1 = mon->w - CLOCK_W;
    tw = (x1 - x0) / n;
    if (tw < 50)
        tw = 50;
    for (c = clients; c; c = c->next) {
        if (c->mon != mon || !(c->tags & mon->curtag) || c->isfullscreen)
            continue;
        if (x >= x0 + i * tw && x < x0 + (i + 1) * tw) {
            /* свёрнутое окно — разворачиваем, затем поднимаем;
             * повторный щелчок в течение 0.4 с — во весь экран */
            if (c->ishidden)
                clientstate(c, NormalState);
            focus(c);
            {
                struct timeval tv;
                long now;
                gettimeofday(&tv, NULL);
                now = (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
                if (c->win == lastbarwin && now - lastbarms < 400) {
                    c->isfullscreen = !c->isfullscreen;
                    arrange();
                    lastbarwin = None;
                } else {
                    lastbarwin = c->win;
                    lastbarms = now;
                }
            }
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
    /* Наша подписка на события окна. Подписка у каждого клиента своя, поэтому
     * приложение от неё ничего не теряет — зато менеджер видит нажатие даже
     * там, где приложение выбрало только свои события (такие окна раньше
     * нельзя было перетащить мышью). */
    XSelectInput(dpy, c->win,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 /* ICCCM/EWMH: WM_CHANGE_STATE и _NET_WM_STATE приходят
                  * в само окно, и услышать их можно только выбрав
                  * SubstructureRedirectMask на нём */
                 SubstructureRedirectMask | SubstructureNotifyMask);

    /* Своих захватов на чужих окнах больше не делаем: они перехватывали
     * события у приложений (кнопки в приложениях переставали нажиматься).
     * Курсор мыши при необходимости захватывается в момент начала
     * перетаскивания, а не заранее. */
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);

    /* с Win окно отрывается от плитки сразу; Win + правая кнопка — размер */
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

/* сворачивание и разворачивание окна средствами менеджера */
static void
clientstate(Client *c, int state)
{
    XChangeProperty(dpy, c->win, wm_state, wm_state, 32, PropModeReplace,
                    (unsigned char *)&state, 1);
    if (state == IconicState) {
        c->ishidden = 1;
        XUnmapWindow(dpy, c->win);
    } else {
        c->ishidden = 0;
        XMapWindow(dpy, c->win);
    }
}


/* Кнопки «свернуть» и «развернуть» в самих приложениях: приложение шлёт
 * WM_CHANGE_STATE или _NET_WM_STATE — раньше мы это игнорировали, и нажатия
 * ничего не делали. */
static void
clientmessage(XEvent *e)
{
    XClientMessageEvent *ev = &e->xclient;
    Client *c;
    Atom a1, a2;
    int action;

    for (c = clients; c; c = c->next)
        if (c->win == ev->window)
            break;
    if (!c || ev->format != 32)
        return;

    /* свернуть */
    if (ev->message_type == wm_change_state && ev->data.l[0] == IconicState) {
        if (!c->ishidden) {
            clientstate(c, IconicState);
            arrange();
            drawbar(m);
        }
        return;
    }

    if (ev->message_type != atoms[NetWMState])
        return;

    /* 0 = убрать состояние, 1 = добавить, 2 = переключить */
    action = (int)ev->data.l[1];
    a1 = (Atom)ev->data.l[0];
    a2 = (Atom)ev->data.l[2];

    /* во весь экран: приложение просит максимизацию или полноэкранный режим */
    if (a1 == atoms[NetWMFullscreen] || a2 == atoms[NetWMFullscreen] ||
        a1 == atoms[NetWMStateMaxH] || a2 == atoms[NetWMStateMaxH] ||
        a1 == atoms[NetWMStateMaxV] || a2 == atoms[NetWMStateMaxV]) {
        int want = (action == 0) ? 0 : (action == 1) ? 1 : !c->isfullscreen;
        if (want != c->isfullscreen) {
            c->isfullscreen = want;
            focus(c);
            arrange();
        }
        return;
    }

    if (a1 == atoms[NetWMStateHidden] || a2 == atoms[NetWMStateHidden]) {
        if (action != 0)
            clientstate(c, IconicState);
        else {
            clientstate(c, NormalState);
            focus(c);
        }
        arrange();
        drawbar(m);
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

    /* дочерние окна (меню, подсказки, всплывающие списки) не становятся
     * самостоятельными окнами: показываем и забываем */
    if (ev->parent != DefaultRootWindow(dpy)) {
        XMapWindow(dpy, ev->window);
        return;
    }

    for (c = clients; c; c = c->next)
        if (c->win == ev->window)
            break;
    if (!c)
        c = addtoclient(ev->window);
    c->ishidden = 0;
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
        /* Перетаскивание мышью: окно отрывается от тайла и едет за курсором.
         * Без Win порог 8 px — иначе обычный клик по кнопке окна ломал бы
         * тайлинг.
         *
         * Курсор у приложения НЕ отбираем: пока порог не пройден, события
         * идут приложению как обычно, поэтому клики работают. Позиция
         * указателя берётся опросом, а не событиями, — порог срабатывает
         * даже если приложение перехватило события мыши. Захват курсора
         * включаем только когда перетаскивание действительно началось. */
        int grab_x = be->x_root, grab_y = be->y_root;
        int dx0 = be->x_root - c->x;
        int dy0 = be->y_root - c->y;
        int moving = (CLEANMASK(be->state) == MODKEY);
        int dragging = 0;
        Window root = DefaultRootWindow(dpy), rr, ch;
        int rx, ry, wx, wy;
    unsigned int mask;
        XEvent te;
        time_t started = time(NULL);

        if (moving)
            c->isfloating = 1;

        while (1) {
            /* кнопка отпущена? (событие придёт и от окна, и от корня) */
            if (XCheckMaskEvent(dpy, ButtonReleaseMask, &te))
                break;

            if (!XQueryPointer(dpy, root, &rr, &ch, &rx, &ry, &wx, &wy, &mask)) {
                XSync(dpy, False);
                if (!XQueryPointer(dpy, root, &rr, &ch, &rx, &ry, &wx, &wy, &mask))
                    break;
            }

            if (!moving) {
                int mdx = rx - grab_x, mdy = ry - grab_y;
                if (mdx > 8 || mdy > 8 || mdx < -8 || mdy < -8)
                    moving = (c->isfloating = 1);
            }
            if (moving) {
                if (!dragging) {
                    XGrabPointer(dpy, c->win, False,
                                 ButtonReleaseMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync, None, None,
                                 CurrentTime);
                    dragging = 1;
                }
                c->x = rx - dx0;
                c->y = ry - dy0;
                c->ox = c->x; c->oy = c->y;
                XMoveWindow(dpy, c->win, c->x, c->y);
            }
            if (time(NULL) - started > 30)      /* страховка от залипания */
                break;
            usleep(12000);
        }
        if (dragging)
            XUngrabPointer(dpy, CurrentTime);
        if (moving)
            arrange();
    } else if (be->button == Button3 && CLEANMASK(be->state) == MODKEY) {
        int sw = c->w, sh = c->h;
        c->isfloating = 1;
        while (1) {
            XEvent te;
            int dx, dy;
            XMaskEvent(dpy, PointerMotionMask | ButtonReleaseMask, &te);
            if (te.type == ButtonRelease)
                break;
            /* на всякий случай: если событий нет больше 30 секунд — выходим,
             * иначе менеджер зависнет намертво */
            dx = te.xmotion.x_root - be->x_root;
            dy = te.xmotion.y_root - be->y_root;
            resize(c, c->x, c->y, MAX(sw + dx, MINW), MAX(sh + dy, MINH));
        }
        c->ow = c->w; c->oh = c->h;
        arrange();
    }
}

static void
unmapnotify(XEvent *e)
{
    XUnmapEvent *ev = &e->xunmap;
    Client *c;

    /* Окно убрано с экрана. Так бывает и при сворачивании, поэтому клиента
     * из списка НЕ удаляем — просто помечаем скрытым: окно живо, его можно
     * вернуть (например, из панели). Настоящее удаление делает destroynotify,
     * когда окно уничтожено по-настоящему. */
    for (c = clients; c; c = c->next)
        if (c->win == ev->window)
            break;
    if (!c)
        return;
    c->ishidden = 1;
    unfocus(c);
    arrange();
    drawbar(m);
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
    if (c->ishidden)
        return;               /* свёрнуто: фокус не возвращаем */
    XGetInputFocus(dpy, &focuswin, &revert);
    if (ce->window != focuswin)
        focus(c);
}

/* Ошибки X (например, обращение к уже закрытому окну) не должны убивать
 * оконный менеджер: иначе закрытие приложения обрывает всю сессию и рабочий
 * стол перезапускается. */
static int
xerrorhandler(Display *dpy, XErrorEvent *e)
{
    (void)dpy;
    (void)e;
    return 0;
}


static void pointercheck(void);

static void
run(void)
{
    XEvent ev;

    while (running) {
        if (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            switch (ev.type) {
            case ConfigureRequest: configurerequest(&ev); break;
            case ClientMessage:    clientmessage(&ev);    break;
            case MapRequest:        maprequest(&ev);       break;
            case ButtonPress:       buttonpress(&ev);      break;
            case UnmapNotify:       unmapnotify(&ev);      break;
            case DestroyNotify:     destroynotify(&ev);    break;
            case KeyPress:          keypress(&ev);         break;
            case EnterNotify:       enternotify(&ev);      break;
            case Expose:            drawbars();            break;
            }
        } else if (time(NULL) - laststatus >= 1) {
            laststatus = time(NULL);
            checkconfig();       /* тема и настройки применяются сразу */
            if (barvisible)
                drawbars();
        } else {
            pointercheck();
            usleep(8000);
        }
    }
}

/* Окно под указателем (самое верхнее видимое). */
static Client *
clientat(int rx, int ry)
{
    Client *c;

    if (m) {
        for (c = clients; c; c = c->next)
            if (c == m->sel && !c->ishidden &&
                rx >= c->x && rx < c->x + c->w && ry >= c->y && ry < c->y + c->h)
                return c;
    }
    for (c = clients; c; c = c->next) {
        if (c->ishidden || c->mon == NULL)
            continue;
        if (rx < c->mon->x || rx >= c->mon->x + c->mon->w)
            continue;
        if (ry < c->mon->y || ry >= c->mon->y + c->mon->h)
            continue;
        if (rx >= c->x && rx < c->x + c->w && ry >= c->y && ry < c->y + c->h)
            return c;
    }
    return NULL;
}

/* Перетаскивание мышью. Нажатие ловить не нужно: приложение может перехватить
 * его раньше нас, и тогда менеджер его просто не увидит. Поэтому следим за
 * состоянием кнопки и положением указателя. Пока указатель не сместился
 * больше порога, курсор остаётся у приложения — нажатия работают как обычно;
 * как только смещение есть, окно отрывается от плитки и едет за курсором. */
static Client *dragc;
static int dragpx, dragpy, dragoffx, dragoffy, dragging;

static void
pointercheck(void)
{
    Window root = DefaultRootWindow(dpy), rr, child;
    int rx, ry, wx, wy;
    unsigned int mask;
    Client *c;
    Monitor *bmon;

    if (!XQueryPointer(dpy, root, &rr, &child, &rx, &ry, &wx, &wy, &mask))
        return;

    /* указатель над панелью — окна не трогаем */
    for (bmon = monitors; bmon; bmon = bmon->next)
        if (bmon->barwin == child)
            return;

    if (!(mask & Button1Mask)) {
        if (dragging) {
            XUngrabPointer(dpy, CurrentTime);
            dragging = 0;
            arrange();
            fprintf(stderr, "lindu-wm: перетаскивание окна завершено\n");
        }
        dragc = NULL;
        return;
    }

    if (dragc) {
        int dx = rx - dragpx, dy = ry - dragpy;
        if (!dragging && (dx > 8 || dy > 8 || dx < -8 || dy < -8)) {
            dragging = 1;
            dragc->isfloating = 1;
            XGrabPointer(dpy, dragc->win, False,
                         ButtonReleaseMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            fprintf(stderr, "lindu-wm: начато перетаскивание окна 0x%lx\n",
                    (unsigned long)dragc->win);
        }
        if (dragging) {
            dragc->x = rx - dragoffx;
            dragc->y = ry - dragoffy;
            dragc->ox = dragc->x;
            dragc->oy = dragc->y;
            XMoveWindow(dpy, dragc->win, dragc->x, dragc->y);
        }
        return;
    }

    c = clientat(rx, ry);
    if (!c)
        return;
    /* Тянем только за верхнюю полосу окна (заголовок). Иначе окно цеплялось бы
     * за любую точку и мешало выделять текст, двигать ползунки и таскать
     * файлы внутри приложений. Порог 8 px оставлен: обычный клик по кнопке
     * приложения окно не сдвинет. */
    if (ry - c->y > opt_dragtop)
        return;
    dragc = c;
    dragpx = rx;
    dragpy = ry;
    dragoffx = rx - c->x;
    dragoffy = ry - c->y;
    focus(c);
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

    cursor = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, DefaultRootWindow(dpy), cursor);

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
    atoms[NetWMStateMaxH]   = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    atoms[NetWMStateMaxV]   = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    atoms[NetWMStateHidden] = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
    atoms[NetWMWindowType]       = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    atoms[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    atoms[NetWMWindowTypeDock]   = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atoms[NetWMWindowTypeDesktop]= XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    atoms[NetWMWindowTypeUtility]= XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    wm_protocols  = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete     = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_state      = XInternAtom(dpy, "WM_STATE", False);
    wm_change_state = XInternAtom(dpy, "WM_CHANGE_STATE", False);

    for (i = 0; i < NetLast; i++)
        supported[i] = atoms[i];
    XChangeProperty(dpy, DefaultRootWindow(dpy), atoms[NetSupported],
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)supported, NetLast);

    barh = BARH;
    /* Второй аргумент — НОМЕР экрана, а не его ширина.
     * И важно: XftFontOpen в современной Xft ждёт пары «ключ, значение»
     * (XFT_FAMILY, XFT_SIZE, …), а строку с шаблоном принимает XftFontOpenName.
     * С неверной функцией шрифт не открывался, и текст панели не рисовался. */
    barxft = XftFontOpenName(dpy, screen, FONT_XFT);
    if (!barxft)
        barxft = XftFontOpenName(dpy, screen, "DejaVu Sans:size=12");
    if (!barxft)
        fprintf(stderr, "lindu-wm: ВНИМАНИЕ: не найден шрифт для панели, "
                        "подписи выводиться не будут (нужен ttf-dejavu)\n");
    else
        fprintf(stderr, "lindu-wm: шрифт панели открыт (%s), высота %d\n",
                FONT_XFT, barxft->height);
    loadconfig();          /* цвета и параметры из theme.conf */
    col_inact  = getcolor(cfg_bar);
    col_bar    = col_inact;
    col_baract = getcolor(cfg_act);
    col_barline= getcolor(cfg_line);
    col_bartxt = getcolor(cfg_text);
    bargc = XCreateGC(dpy, DefaultRootWindow(dpy), 0, NULL);

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
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 EnterWindowMask | KeyPressMask);
    grabkeys();

    applybar();                 /* высота и видимость панели из настроек */
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
    XSetErrorHandler(xerrorhandler);

    /* окружение рабочего стола объявляем сами: приложения, запущенные из
     * оконного менеджера, получают эти переменные независимо от того,
     * как стартовала сессия (важно для уже установленной системы). */
    setenv("XDG_CURRENT_DESKTOP", "LINDU", 1);
    setenv("XDG_SESSION_DESKTOP", "lindudesktop", 1);
    setenv("XDG_SESSION_TYPE", "x11", 1);
    setenv("DESKTOP_SESSION", "lindu", 1);

    setup();
    run();

    XCloseDisplay(dpy);
    return 0;
}
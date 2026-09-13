/*
 * lindu-wm: конфигурация.
 * Измените значения и пересоберите: make && sudo make install
 */

#include <X11/keysym.h>

#define VERSION   "1.0"
#define TITLE     "lindu linux"
#define HOSTNAME  "lindu"

/* ---------------------------- панель ---------------------------------- */

#define BARH      26                 /* высота панели в пикселях   */
#define PADDING_X 8
#define PADDING_Y 7
#define FONT      "-*-terminus-medium-r-normal-*-14-*-*-*-*-*-*-*"

#define ACTIVE    "#000000"          /* цвет панели               */
#define INACTIVE  "#1a1b1e"          /* не используется напрямую  */
#define ACCENT    "#79d7f0"          /* цвет текста               */

/* --------------------------- раскладки --------------------------------- */

#define MASTERFACTOR 55              /* доля мастера, %           */
#define MINW  80                     /* минимальная ширина окна   */
#define MINH  40

/* -------------------------- модификаторы -------------------------------- */

#define MODKEY Mod4Mask              /* Super (Windows) */
#define TAGKEYS(K, T) \
    { MODKEY,                       K, view,       {.ui = 1 << T} }, \
    { MODKEY | ShiftMask,           K, toggletag,  {.ui = 1 << T} }, \
    { MODKEY | ControlMask,         K, tagclient,  {.ui = 1 << T} }

#define SHCMD(cmd) { .v = (const char[]){ cmd } }

/* ----------------------------- клавиши ---------------------------------- */

static Key keys[] = {
    /* запуск программ */
    { MODKEY, XK_Return,        spawn,          SHCMD("alacritty") },
    { MODKEY, XK_d,             spawn,          SHCMD("rofi -show drun") },
    { MODKEY | ShiftMask, XK_b, spawn,          SHCMD("firefox") },
    { MODKEY, XK_e,             spawn,          SHCMD("thunar") },
    { MODKEY, XK_p,             spawn,          SHCMD("scrot ~/Pictures/%Y-%m-%d_%H-%M-%S.png") },

    /* выход и окна */
    { MODKEY, XK_q,             quit,           {0} },
    { MODKEY | ShiftMask, XK_q, quit,           {0} },
    { MODKEY | ShiftMask, XK_c, killclient,      {0} },
    { MODKEY, XK_space,         togglefloat,     {0} },
    { MODKEY, XK_f,             togglefullscreen,{0} },

    /* раскладки */
    { MODKEY, XK_t,             setlayout,      {.ui = LT_TILE} },
    { MODKEY, XK_m,             setlayout,      {.ui = LT_MONOCLE} },
    { MODKEY, XK_x,             setlayout,      {.ui = LT_FLOAT} },
    { MODKEY, XK_b,             togglebar,      {0} },

    /* фокус и перемещение */
    { MODKEY, XK_j,             focusnext,      {0} },
    { MODKEY, XK_k,             focusprev,      {0} },
    { MODKEY | ShiftMask, XK_j, movestack,      {.i = +1} },
    { MODKEY | ShiftMask, XK_k, movestack,      {.i = -1} },

    /* размеры */
    { MODKEY, XK_h,             resizemaster,   {.i = -50} },
    { MODKEY, XK_l,             resizemaster,   {.i = +50} },
    { MODKEY | ShiftMask, XK_h, incrementx,     {.i = +50} },
    { MODKEY | ShiftMask, XK_l, incrementx,     {.i = -50} },

    /* навигация по мониторам */
    { MODKEY, XK_Right,         focusmon,       {.i = +1} },
    { MODKEY, XK_Left,          focusmon,       {.i = -1} },
    { MODKEY | ShiftMask, XK_Right, tagmon,     {.i = +1} },
    { MODKEY | ShiftMask, XK_Left,  tagmon,     {.i = -1} },

    /* теги */
    TAGKEYS(XK_1, T1) TAGKEYS(XK_2, T2) TAGKEYS(XK_3, T3)
    TAGKEYS(XK_4, T4) TAGKEYS(XK_5, T5) TAGKEYS(XK_6, T6)
    TAGKEYS(XK_7, T7) TAGKEYS(XK_8, T8) TAGKEYS(XK_9, T9)
};
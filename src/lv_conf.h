#if 1  /* Set to "1" to enable this file */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Résolution */
#define LV_HOR_RES_MAX  800
#define LV_VER_RES_MAX  480

/* Profondeur couleur : 16 bits (RGB565) */
#define LV_COLOR_DEPTH  16
#define LV_COLOR_16_SWAP 0

/* Mémoire LVGL (allouée dans PSRAM via lv_mem_alloc) */
#define LV_MEM_CUSTOM    1
#if LV_MEM_CUSTOM
#  define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#  define LV_MEM_CUSTOM_ALLOC   malloc
#  define LV_MEM_CUSTOM_FREE    free
#  define LV_MEM_CUSTOM_REALLOC realloc
#else
#  define LV_MEM_SIZE  (256 * 1024U)
#endif

/* Taille du buffer de rendu par écran (en pixels) */
#define LV_DISP_DEF_REFR_PERIOD  10    /* ms */

/* Logging */
#define LV_USE_LOG       0

/* Animations */
#define LV_USE_ANIMATION 1
#define LV_ANIM_RESOLUTION 1024

/* Widgets nécessaires */
#define LV_USE_ARC       1
#define LV_USE_BAR       1
#define LV_USE_BTN       1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS    0
#define LV_USE_CHECKBOX  0
#define LV_USE_DROPDOWN  0
#define LV_USE_IMG       0
#define LV_USE_LABEL     1
#define LV_USE_LINE      0
#define LV_USE_ROLLER    0
#define LV_USE_SLIDER    1
#define LV_USE_SWITCH    1
#define LV_USE_TEXTAREA  1
#define LV_USE_TABLE     0

/* Thème */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* Polices */
#define LV_FONT_MONTSERRAT_10  0
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  0
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_22  0
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_26  0
#define LV_FONT_MONTSERRAT_28  0
#define LV_FONT_MONTSERRAT_30  0
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_34  0
#define LV_FONT_MONTSERRAT_36  0
#define LV_FONT_MONTSERRAT_38  0
#define LV_FONT_MONTSERRAT_40  0
#define LV_FONT_MONTSERRAT_42  0
#define LV_FONT_MONTSERRAT_44  0
#define LV_FONT_MONTSERRAT_46  0
#define LV_FONT_MONTSERRAT_48  1

#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* Widgets "extra" — désactiver ceux dont les dépendances sont désactivées */
#define LV_USE_ANIMIMG    0   // dépend de LV_USE_IMG
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0   // dépend de LV_USE_IMG
#define LV_USE_KEYBOARD   1   // dépend de LV_USE_BTNMATRIX
#define LV_USE_LED        0
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0   // dépend de LV_USE_TEXTAREA
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/* Divers */
#define LV_USE_SNAPSHOT    0
#define LV_USE_MONKEY      0
#define LV_USE_GRIDNAV     0
#define LV_USE_FRAGMENT    0
#define LV_USE_IMGFONT     0
#define LV_USE_MSG         0
#define LV_USE_IME_PINYIN  0
#define LV_USE_FLEX   1
#define LV_USE_GRID   1

#define LV_SPRINTF_CUSTOM 0

#endif /* LV_CONF_H */
#endif /* End enable file */

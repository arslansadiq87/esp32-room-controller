#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
/* Settings panels and their dropdown lists can exceed the fixed 64 KiB pool. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_USE_LOG 0

#endif

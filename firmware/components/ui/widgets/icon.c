// SPDX-License-Identifier: AGPL-3.0-or-later
// Original line icons on a 24-unit grid; drawn with vector primitives so one shape serves every size.
#include "widgets.h"
#include "theme_tokens.h"
#include <math.h>
#define PI 3.14159265f
typedef struct {
    lv_layer_t *layer;
    float x, y, scale;
    int32_t stroke;
    lv_color_t color, back;  // back: whatever is behind the icon, for cut-out shapes
} pen_t;
static int32_t px(const pen_t *pen, float value, float origin) { return (int32_t)lroundf(origin + value * pen->scale); }
static void line(const pen_t *pen, float x1, float y1, float x2, float y2)
{
    lv_draw_line_dsc_t dsc; lv_draw_line_dsc_init(&dsc);
    dsc.color = pen->color; dsc.width = pen->stroke; dsc.round_start = 1; dsc.round_end = 1;
    dsc.p1.x = px(pen, x1, pen->x); dsc.p1.y = px(pen, y1, pen->y);
    dsc.p2.x = px(pen, x2, pen->x); dsc.p2.y = px(pen, y2, pen->y);
    lv_draw_line(pen->layer, &dsc);
}
static void polyline(const pen_t *pen, const float *points, unsigned count)
{
    for (unsigned i = 1; i < count; ++i) line(pen, points[i * 2 - 2], points[i * 2 - 1], points[i * 2], points[i * 2 + 1]);
}
// LVGL angles: 0 = east, growing clockwise (screen y points down).
static void arc(const pen_t *pen, float cx, float cy, float r, int start, int end)
{
    lv_draw_arc_dsc_t dsc; lv_draw_arc_dsc_init(&dsc);
    dsc.color = pen->color; dsc.width = pen->stroke; dsc.rounded = 1;
    dsc.center.x = px(pen, cx, pen->x); dsc.center.y = px(pen, cy, pen->y);
    dsc.radius = (uint16_t)lroundf(r * pen->scale + pen->stroke / 2.0f);
    dsc.start_angle = start; dsc.end_angle = end;
    lv_draw_arc(pen->layer, &dsc);
}
static void disc(const pen_t *pen, float cx, float cy, float r, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color; dsc.radius = LV_RADIUS_CIRCLE;
    lv_area_t area = {px(pen, cx - r, pen->x), px(pen, cy - r, pen->y), px(pen, cx + r, pen->x), px(pen, cy + r, pen->y)};
    lv_draw_rect(pen->layer, &dsc, &area);
}
static void bar(const pen_t *pen, float x1, float y1, float x2, float y2)
{
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = pen->color; dsc.radius = (int32_t)lroundf(pen->scale);
    lv_area_t area = {px(pen, x1, pen->x), px(pen, y1, pen->y), px(pen, x2, pen->x), px(pen, y2, pen->y)};
    lv_draw_rect(pen->layer, &dsc, &area);
}
static void triangle(const pen_t *pen, float x1, float y1, float x2, float y2, float x3, float y3)
{
    lv_draw_triangle_dsc_t dsc; lv_draw_triangle_dsc_init(&dsc);
    dsc.color = pen->color;
    dsc.p[0].x = px(pen, x1, pen->x); dsc.p[0].y = px(pen, y1, pen->y);
    dsc.p[1].x = px(pen, x2, pen->x); dsc.p[1].y = px(pen, y2, pen->y);
    dsc.p[2].x = px(pen, x3, pen->x); dsc.p[2].y = px(pen, y3, pen->y);
    lv_draw_triangle(pen->layer, &dsc);
}
static void rays(const pen_t *pen, float cx, float cy, float inner, float outer)
{
    for (int i = 0; i < 8; ++i) {
        float angle = i * PI / 4;
        line(pen, cx + cosf(angle) * inner, cy + sinf(angle) * inner, cx + cosf(angle) * outer, cy + sinf(angle) * outer);
    }
}
// Cloud outline whose flat base sits on y = base; the bumps are blacked out first so shapes behind stay hidden.
static void cloud(const pen_t *pen, float dx, float base)
{
    pen_t fill = *pen; fill.color = pen->back;
    disc(pen, dx + 7, base - 4, 4, pen->back); disc(pen, dx + 12, base - 8, 5, pen->back); disc(pen, dx + 17, base - 4, 4, pen->back);
    bar(&fill, dx + 7, base - 6, dx + 17, base);
    arc(pen, dx + 7, base - 4, 4, 90, 270);
    arc(pen, dx + 12, base - 8, 5, 180, 360);
    arc(pen, dx + 17, base - 4, 4, 270, 90);
    line(pen, dx + 7, base, dx + 17, base);
}
static void moon(const pen_t *pen, float cx, float cy, float r)
{
    disc(pen, cx, cy, r, pen->color);
    disc(pen, cx + r * 0.55f, cy - r * 0.4f, r * 0.85f, pen->back);
}
static void bolt(const pen_t *pen, float dx, float dy)
{
    triangle(pen, dx + 13, dy + 2, dx + 5, dy + 13, dx + 12.5f, dy + 13);
    triangle(pen, dx + 11, dy + 22, dx + 19, dy + 10, dx + 11.5f, dy + 10);
    bar(pen, dx + 10, dy + 10, dx + 14, dy + 13);
}
static void drops(const pen_t *pen, float length, bool flakes)
{
    for (int i = 0; i < 3; ++i) {
        float x = 8 + i * 4.5f;
        if (flakes) disc(pen, x, 20, 1.3f, pen->color);
        else line(pen, x + 1, 18.5f, x, 18.5f + length);
    }
}
static void draw(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t area; lv_obj_get_coords(obj, &area);
    float size = (float)lv_area_get_width(&area);
    pen_t pen = {lv_event_get_layer(event), (float)area.x1, (float)area.y1, size / 24, (int32_t)LV_MAX(2, lroundf(size / 12)),
        lv_obj_get_style_text_color(obj, 0), lv_color_hex(NUKA_COLOR_BG)};
    for (lv_obj_t *parent = lv_obj_get_parent(obj); parent; parent = lv_obj_get_parent(parent))
        if (lv_obj_get_style_bg_opa(parent, 0) == LV_OPA_COVER) { pen.back = lv_obj_get_style_bg_color(parent, 0); break; }
    pen_t sun = pen, water = pen;
    sun.color = lv_color_hex(NUKA_COLOR_WARN); water.color = lv_color_hex(NUKA_COLOR_INFO);
    switch ((ui_icon_t)(intptr_t)lv_obj_get_user_data(obj)) {
    case ICON_WIFI:
        arc(&pen, 12, 19, 13, 225, 315); arc(&pen, 12, 19, 8, 225, 315); disc(&pen, 12, 19, 2, pen.color); break;
    case ICON_OFFLINE:
        arc(&pen, 12, 19, 13, 225, 315); arc(&pen, 12, 19, 8, 225, 315); disc(&pen, 12, 19, 2, pen.color); line(&pen, 4, 4, 20, 20); break;
    case ICON_BOLT: bolt(&pen, 0, 0); break;
    case ICON_MOON: moon(&pen, 11, 13, 8); break;
    case ICON_PLAY: triangle(&pen, 7, 4, 7, 20, 20, 12); break;
    case ICON_PAUSE: bar(&pen, 6, 4, 10, 20); bar(&pen, 14, 4, 18, 20); break;
    case ICON_NEXT: triangle(&pen, 4, 5, 4, 19, 15, 12); bar(&pen, 16, 5, 19, 19); break;
    case ICON_PREV: triangle(&pen, 20, 5, 20, 19, 9, 12); bar(&pen, 5, 5, 8, 19); break;
    case ICON_CHEVRON: polyline(&pen, (const float[]){9, 5, 16, 12, 9, 19}, 3); break;
    case ICON_BACK: polyline(&pen, (const float[]){15, 5, 8, 12, 15, 19}, 3); break;
    case ICON_LOCK:
        arc(&pen, 12, 10, 5, 180, 360); line(&pen, 7, 10, 7, 11); line(&pen, 17, 10, 17, 11);
        polyline(&pen, (const float[]){5, 11, 19, 11, 19, 21, 5, 21, 5, 11}, 5); disc(&pen, 12, 16, 1.5f, pen.color); break;
    case ICON_UP: line(&pen, 12, 20, 12, 4); polyline(&pen, (const float[]){6, 10, 12, 4, 18, 10}, 3); break;
    case ICON_DOWN: line(&pen, 12, 4, 12, 20); polyline(&pen, (const float[]){6, 14, 12, 20, 18, 14}, 3); break;
    case ICON_CHECK: polyline(&pen, (const float[]){5, 12.5f, 10, 17.5f, 19, 7}, 3); break;
    case ICON_CROSS: line(&pen, 6, 6, 18, 18); line(&pen, 18, 6, 6, 18); break;
    case ICON_HOST:
        polyline(&pen, (const float[]){3, 4, 21, 4, 21, 16, 3, 16, 3, 4}, 5); line(&pen, 12, 16, 12, 20); line(&pen, 8, 20, 16, 20); break;
    case ICON_SERVER:
        polyline(&pen, (const float[]){4, 4, 20, 4, 20, 10.5f, 4, 10.5f, 4, 4}, 5);
        polyline(&pen, (const float[]){4, 13.5f, 20, 13.5f, 20, 20, 4, 20, 4, 13.5f}, 5);
        disc(&pen, 8, 7.25f, 1.2f, pen.color); disc(&pen, 8, 16.75f, 1.2f, pen.color); break;
    case ICON_SPARK: for (int i = 0; i < 4; ++i) { float a = i * PI / 4; line(&pen, 12 - cosf(a) * 9, 12 - sinf(a) * 9, 12 + cosf(a) * 9, 12 + sinf(a) * 9); } break;
    case ICON_HEX: polyline(&pen, (const float[]){12, 3, 20, 7.5f, 20, 16.5f, 12, 21, 4, 16.5f, 4, 7.5f, 12, 3}, 7); break;
    case ICON_STAR:
        triangle(&pen, 12, 2, 9.5f, 12, 14.5f, 12); triangle(&pen, 12, 22, 9.5f, 12, 14.5f, 12);
        triangle(&pen, 2, 12, 12, 9.5f, 12, 14.5f); triangle(&pen, 22, 12, 12, 9.5f, 12, 14.5f); break;
    case ICON_DOT: disc(&pen, 12, 12, 7, pen.color); break;
    case ICON_RESET: arc(&pen, 12, 12, 8, 45, 315); polyline(&pen, (const float[]){13, 6.3f, 17.7f, 6.3f, 17.7f, 1.6f}, 3); break;
    case ICON_GEAR: {
        pen_t tooth = pen; tooth.stroke = pen.stroke * 2;
        for (int i = 0; i < 8; ++i) { float a = i * PI / 4 + PI / 8; line(&tooth, 12 + cosf(a) * 7, 12 + sinf(a) * 7, 12 + cosf(a) * 9.2f, 12 + sinf(a) * 9.2f); }
        arc(&pen, 12, 12, 6.5f, 0, 360); arc(&pen, 12, 12, 2.5f, 0, 360); break;
    }
    case ICON_PLUS: line(&pen, 5, 12, 19, 12); line(&pen, 12, 5, 12, 19); break;
    case ICON_MINUS: line(&pen, 5, 12, 19, 12); break;
    case ICON_MUSIC: line(&pen, 9, 18, 9, 5); line(&pen, 9, 5, 19, 3); line(&pen, 19, 3, 19, 16); disc(&pen, 6.5f, 18, 3, pen.color); disc(&pen, 16.5f, 16, 3, pen.color); break;
    case ICON_BELL: arc(&pen, 12, 11, 6, 180, 360); line(&pen, 6, 11, 6, 16); line(&pen, 18, 11, 18, 16); line(&pen, 4, 17, 20, 17); disc(&pen, 12, 20.5f, 1.8f, pen.color); break;
    case ICON_POWER: arc(&pen, 12, 13, 8, 300, 240); line(&pen, 12, 3, 12, 12); break;
    case ICON_BRIGHT: disc(&pen, 12, 12, 3.5f, pen.color); rays(&pen, 12, 12, 6.5f, 9); break;
    case ICON_TIMER: arc(&pen, 12, 13.5f, 8, 0, 360); line(&pen, 12, 13.5f, 12, 9); line(&pen, 10, 2.5f, 14, 2.5f); break;
    case ICON_ALERT: polyline(&pen, (const float[]){12, 3, 21.5f, 20, 2.5f, 20, 12, 3}, 4); line(&pen, 12, 9, 12, 13.5f); disc(&pen, 12, 17, 1.4f, pen.color); break;
    case ICON_SUN: arc(&sun, 12, 12, 4.5f, 0, 360); rays(&sun, 12, 12, 7.5f, 10); break;
    case ICON_NIGHT: moon(&pen, 11, 13, 8); break;
    case ICON_PARTLY: arc(&sun, 9, 8, 3.5f, 0, 360); rays(&sun, 9, 8, 5.5f, 7.5f); cloud(&pen, 2, 21); break;
    case ICON_CLOUD: cloud(&pen, 0, 19); break;
    case ICON_FOG: line(&pen, 3, 8, 21, 8); line(&pen, 5, 12, 19, 12); line(&pen, 3, 16, 21, 16); line(&pen, 7, 20, 17, 20); break;
    case ICON_DRIZZLE: cloud(&pen, 0, 15); drops(&water, 1.5f, false); break;
    case ICON_RAIN: cloud(&pen, 0, 15); drops(&water, 3.5f, false); break;
    case ICON_HEAVY_RAIN: cloud(&pen, 0, 15); drops(&water, 5, false); line(&water, 19, 17, 18, 21); break;
    case ICON_SNOW: cloud(&pen, 0, 15); drops(&water, 0, true); break;
    case ICON_SLEET: cloud(&pen, 0, 15); line(&water, 9, 18.5f, 8, 21.5f); disc(&water, 12.5f, 20, 1.3f, water.color); line(&water, 18, 18.5f, 17, 21.5f); break;
    case ICON_THUNDER: cloud(&pen, 0, 13); bolt(&(pen_t){sun.layer, sun.x + 2 * sun.scale, sun.y + 6 * sun.scale, sun.scale * 0.7f, sun.stroke, sun.color}, 0, 0); break;
    case ICON_WIND:
        line(&pen, 3, 9, 15, 9); arc(&pen, 15, 6.5f, 2.5f, 270, 90);
        line(&pen, 3, 14, 18, 14); arc(&pen, 18, 16.5f, 2.5f, 270, 90);
        line(&pen, 3, 19, 11, 19); break;
    }
}
lv_obj_t *ui_icon(lv_obj_t *parent, ui_icon_t icon, int size, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(obj, size, size);
    lv_obj_add_event_cb(obj, draw, LV_EVENT_DRAW_MAIN, NULL);
    ui_icon_set(obj, icon, color);
    return obj;
}
void ui_icon_set(lv_obj_t *icon, ui_icon_t value, uint32_t color)
{
    if ((ui_icon_t)(intptr_t)lv_obj_get_user_data(icon) == value && lv_color_to_u32(lv_obj_get_style_text_color(icon, 0)) == lv_color_to_u32(lv_color_hex(color))) return;
    lv_obj_set_user_data(icon, (void *)(intptr_t)value);
    lv_obj_set_style_text_color(icon, lv_color_hex(color), 0);
    lv_obj_invalidate(icon);
}

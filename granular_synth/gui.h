#pragma once

#include <stdio.h>

#include "smol_canvas.h"
#include "smol_utils.h"

// yoinked this from stackoverflow
unsigned long hash(unsigned char* str);

typedef unsigned long widget_id_t;

typedef struct point_t {
    int x, y;
} point_t;

typedef struct rect_t {
    int x, y, width, height;
} rect_t;

#define rect(x, y, w, h) (rect_t){ x, y, w, h }

int rect_has_point(rect_t rect, point_t point);

typedef struct rectcut_t {
    int minx, miny, maxx, maxy;
} rectcut_t;

rectcut_t rectcut_left(rectcut_t* rect, int a);
rectcut_t rectcut_right(rectcut_t* rect, int a);
rectcut_t rectcut_top(rectcut_t* rect, int a);
rectcut_t rectcut_bottom(rectcut_t* rect, int a);
void rectcut_expand(rectcut_t* rect, int a);

typedef struct gui_t {
    widget_id_t activeId, hoveredId;

    point_t mousePosition, mouseDelta;
    int mouseClicked;

    smol_canvas_t* canvas;
} gui_t;

typedef enum _gui_button_state {
    GUI_BUTTON_STATE_IDLE = 0,
    GUI_BUTTON_STATE_HOVERED,
    GUI_BUTTON_STATE_ACTIVE
} gui_button_state;

typedef enum _gui_drag_axis {
    GUI_DRAG_AXIS_X = (1 << 0),
    GUI_DRAG_AXIS_Y = (1 << 1),
    GUI_DRAG_AXIS_XY = GUI_DRAG_AXIS_X | GUI_DRAG_AXIS_Y
} gui_drag_axis;

void gui_init(gui_t* gui, smol_canvas_t* canvas);
void gui_begin(gui_t* gui);
void gui_end(gui_t* gui);

void gui_input_mouse_move(gui_t* gui, point_t position, point_t delta);
void gui_input_mouse_click(gui_t* gui, int clicked);

int gui_is_mouse_down(gui_t* gui, widget_id_t wid);
int gui_clickable_area(gui_t* gui, const char* id, rect_t bounds);
int gui_xdrag_area(gui_t* gui, const char* id, rect_t bounds);
int gui_ydrag_area(gui_t* gui, const char* id, rect_t bounds);
int gui_draggable_area(gui_t* gui, const char* id, gui_drag_axis axis, rect_t bounds, int* outX, int* outY, double xStep, double yStep);

void gui_draw_button(gui_t* gui, const char* text, gui_button_state buttonState, rect_t bounds);

int gui_button(
    gui_t* gui, const char* id, const char* text,
    rect_t bounds
);

int gui_button_toggle(
    gui_t* gui, const char* id, const char* text,
    rect_t bounds,
    int* value
);

int gui_spinner(
    gui_t* gui,
    const char* id,
    rect_t bounds,
    int* value,
    int minValue, int maxValue,
    int step,
    const char* valueFormat
);

int gui_spinnerf(
    gui_t* gui,
    const char* id,
    rect_t bounds,
    float* value,
    float minValue, float maxValue,
    float step,
    const char* valueFormat
);

int gui_spinnerd(
    gui_t* gui,
    const char* id,
    rect_t bounds,
    double* value,
    double minValue, double maxValue,
    double step,
    const char* valueFormat
);

#ifdef GUI_IMPL
#define torect(rectcut) (rect_t){ rectcut.minx, rectcut.miny, rectcut.maxx - rectcut.minx, rectcut.maxy - rectcut.miny }
#define fromrect(rec) (rectcut_t){ rec.x, rec.y, rec.width + rec.x, rec.height + rec.y }

unsigned long hash(unsigned char* str) {
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

int rect_has_point(rect_t rect, point_t point) {
    return point.x >= rect.x &&
        point.x <= rect.x + rect.width &&
        point.y >= rect.y &&
        point.y <= rect.y + rect.height;
}

rectcut_t rectcut_left(rectcut_t* rect, int a) {
    int minx = rect->minx;
    rect->minx = min(rect->maxx, rect->minx + a);
    return (rectcut_t) { minx, rect->miny, rect->minx, rect->maxy };
}

rectcut_t rectcut_right(rectcut_t* rect, int a) {
    int maxx = rect->maxx;
    rect->maxx = max(rect->minx, rect->maxx - a);
    return (rectcut_t) { rect->maxx, rect->miny, maxx, rect->maxy };
}

rectcut_t rectcut_top(rectcut_t* rect, int a) {
    int miny = rect->miny;
    rect->miny = min(rect->maxy, rect->miny + a);
    return (rectcut_t) { rect->minx, miny, rect->maxx, rect->miny };
}

rectcut_t rectcut_bottom(rectcut_t* rect, int a) {
    int maxy = rect->maxy;
    rect->maxy = max(rect->miny, rect->maxy - a);
    return (rectcut_t) { rect->minx, rect->maxy, rect->maxx, maxy };
}

void rectcut_expand(rectcut_t* rect, int a) {
    rect->minx -= a;
    rect->miny -= a;
    rect->maxx += a;
    rect->maxy += a;
}

void gui_init(gui_t* gui, smol_canvas_t* canvas) {
    gui->canvas = canvas;
    gui->activeId = 0;
    gui->hoveredId = 0;
    gui->mouseClicked = 0;
    gui->mousePosition.x = 0;
    gui->mousePosition.y = 0;
}

void gui_begin(gui_t* gui) {
    if (!gui->mouseClicked) gui->hoveredId = 0;
}

void gui_end(gui_t* gui) {
    if (!gui->mouseClicked) gui->activeId = 0;
    gui->mouseDelta.x = 0;
    gui->mouseDelta.y = 0;
}

void gui_input_mouse_move(gui_t* gui, point_t position, point_t delta) {
    gui->mousePosition.x = position.x;
    gui->mousePosition.y = position.y;
    gui->mouseDelta.x = delta.x;
    gui->mouseDelta.y = delta.y;
}

void gui_input_mouse_click(gui_t* gui, int clicked) {
    gui->mouseClicked = clicked;
}

int gui_is_mouse_down(gui_t* gui, widget_id_t wid) {
    return gui->hoveredId == wid && gui->activeId == wid;
}

int gui_clickable_area(gui_t* gui, const char* id, rect_t bounds) {
    widget_id_t wid = hash(id);

    if (rect_has_point(bounds, gui->mousePosition)) {
        gui->hoveredId = wid;

        if (gui->mouseClicked && gui->activeId == 0) {
            gui->activeId = wid;
        }
    }

    if (!gui->mouseClicked && gui_is_mouse_down(gui, wid)) {
        return 1;
    }

    return 0;
}

int gui_xdrag_area(gui_t* gui, const char* id, rect_t bounds) {
    widget_id_t wid = hash(id);

    if (rect_has_point(bounds, gui->mousePosition)) {
        gui->hoveredId = wid;

        if (gui->mouseClicked && gui->activeId == 0) {
            gui->activeId = wid;
        }
    }

    if (gui->mouseClicked && gui_is_mouse_down(gui, wid) && abs(gui->mouseDelta.x) > 0) {
        return gui->mouseDelta.x;
    }

    return 0;
}

int gui_ydrag_area(gui_t* gui, const char* id, rect_t bounds) {
    widget_id_t wid = hash(id);

    if (rect_has_point(bounds, gui->mousePosition)) {
        gui->hoveredId = wid;

        if (gui->mouseClicked && gui->activeId == 0) {
            gui->activeId = wid;
        }
    }

    if (gui->mouseClicked && gui_is_mouse_down(gui, wid) && abs(gui->mouseDelta.y) > 0) {
        return gui->mouseDelta.y;
    }

    return 0;
}

int gui_draggable_area(gui_t* gui, const char* id, gui_drag_axis axis, rect_t bounds, int* outX, int* outY, double xStep, double yStep) {
    widget_id_t wid = hash(id);

    if (rect_has_point(bounds, gui->mousePosition)) {
        gui->hoveredId = wid;

        if (gui->mouseClicked && gui->activeId == 0) {
            gui->activeId = wid;
        }
    }

    int result = 0;
    if (gui_is_mouse_down(gui, wid)) {
        if (axis & GUI_DRAG_AXIS_X == GUI_DRAG_AXIS_X) {
            if (outX) *outX += gui->mouseDelta.x * xStep;
            result = 1;
        }
        if (axis & GUI_DRAG_AXIS_Y == GUI_DRAG_AXIS_Y) {
            if (outY) *outY += gui->mouseDelta.y * yStep;
            result = 1;
        }
    }

    return result;
}

void gui_draw_button(gui_t* gui, const char* text, gui_button_state buttonState, rect_t bounds) {
    smol_canvas_t* canvas = gui->canvas;

    smol_canvas_push_color(canvas);
    switch (buttonState) {
        case GUI_BUTTON_STATE_IDLE: smol_canvas_set_color(canvas, SMOL_RGB(30, 30, 30)); break;
        case GUI_BUTTON_STATE_HOVERED: smol_canvas_set_color(canvas, SMOLC_DARK_GREY); break;
        case GUI_BUTTON_STATE_ACTIVE: smol_canvas_set_color(canvas, SMOLC_DARK_GREEN); break;
    }
    smol_canvas_fill_rect(canvas, bounds.x, bounds.y, bounds.width, bounds.height);

    smol_canvas_set_color(canvas, SMOLC_WHITE);
    smol_canvas_draw_rect(canvas, bounds.x, bounds.y, bounds.width, bounds.height);

    int w, h;
    smol_text_size(canvas, 1, text, &w, &h);

    smol_canvas_push_scissor(canvas);
    smol_canvas_set_scissor(canvas, bounds.x, bounds.y, bounds.width, bounds.height);

    smol_canvas_draw_text(
        canvas,
        bounds.x + (bounds.width / 2 - w / 2),
        bounds.y + (bounds.height / 2 - h / 2) + 1,
        1,
        text
    );
    smol_canvas_pop_scissor(canvas);

    smol_canvas_pop_color(canvas);
}

static gui_button_state gui_button_state_from_gui(gui_t* gui, const char* id) {
    widget_id_t wid = hash(id);
    gui_button_state state = GUI_BUTTON_STATE_IDLE;
    if (gui->hoveredId == wid) {
        state = GUI_BUTTON_STATE_HOVERED;
        if (gui->activeId == wid) {
            state = GUI_BUTTON_STATE_ACTIVE;
        }
    }
    return state;
}

int gui_button(
    gui_t* gui, const char* id, const char* text,
    rect_t bounds
) {
    smol_canvas_t* canvas = gui->canvas;
    widget_id_t wid = hash(id);

    int result = gui_clickable_area(gui, id, bounds);

    gui_draw_button(gui, text, gui_button_state_from_gui(gui, id), bounds);

    return result;
}

int gui_button_toggle(
    gui_t* gui, const char* id, const char* text,
    rect_t bounds,
    int* value
) {
    smol_canvas_t* canvas = gui->canvas;
    widget_id_t wid = hash(id);

    int result = gui_clickable_area(gui, id, bounds);
    if (result) {
        *value = !(*value);
    }

    gui_button_state state = gui_button_state_from_gui(gui, id);
    if (*value) {
        state = GUI_BUTTON_STATE_ACTIVE;
    }
    gui_draw_button(gui, text, state, bounds);

    return result;
}

typedef struct _spinner_result_t {
    int decrement, increment, delta;
    rect_t decBounds, incBounds;
    const char* decId;
    const char* incId;
} _spinner_result_t;

static _spinner_result_t gui_spinner_base(
    gui_t* gui,
    const char* id,
    rect_t bounds,
    const char* valueText
) {
    const int buttonWidth = 24;

    smol_canvas_t* canvas = gui->canvas;
    widget_id_t wid = hash(id);

    rectcut_t root = fromrect(bounds);
    rectcut_t buttonRect = rectcut_right(&root, buttonWidth);

    bounds = torect(root);

    rectcut_t topButton = rectcut_top(&buttonRect, bounds.height / 2);
    rectcut_t botButton = buttonRect;

    smol_canvas_push_color(canvas);

    smol_canvas_set_color(canvas, SMOL_RGB(20, 20, 20));
    smol_canvas_fill_rect(canvas, bounds.x, bounds.y, bounds.width, bounds.height);
    smol_canvas_set_color(canvas, SMOLC_WHITE);
    smol_canvas_draw_rect(canvas, bounds.x, bounds.y, bounds.width, bounds.height);

    int w, h;
    smol_text_size(canvas, 1, valueText, &w, &h);

    smol_canvas_draw_text(
        canvas,
        bounds.x + (bounds.width / 2 - w / 2),
        bounds.y + (bounds.height / 2 - h / 2) + 1,
        1,
        valueText
    );

    smol_canvas_pop_color(canvas);

    char decId[1024], incId[1024];
    sprintf(decId, "%s$$dec", id);
    sprintf(incId, "%s$$inc", id);

    rect_t incBounds = torect(topButton);
    rect_t decBounds = torect(botButton);

    int resultInc = gui_clickable_area(gui, incId, incBounds);
    int resultDec = gui_clickable_area(gui, decId, decBounds);
    int delta = gui_xdrag_area(gui, id, bounds);

    gui_draw_button(gui, "-", gui_button_state_from_gui(gui, decId), decBounds);
    gui_draw_button(gui, "+", gui_button_state_from_gui(gui, incId), incBounds);

    return (_spinner_result_t) {
        resultDec, resultInc, delta,
        decBounds, incBounds,
        decId, incId
    };
}

#define GUI_SPINNER_SENS 0.5

#define gui_spinner_def(suffix, type) \
int gui_##suffix( \
    gui_t* gui, \
    const char* id, \
    rect_t bounds, \
    type* value, \
    type minValue, type maxValue, \
    type step, \
    const char* valueFormat \
) { \
    int changed = 0; \
    char textBuf[1024]; \
    sprintf(textBuf, valueFormat, *value); \
    _spinner_result_t result = gui_spinner_base(gui, id, bounds, textBuf); \
    type oldValue = *value; \
    if (result.decrement) { \
        *value -= step; \
    } \
    else if (result.increment) { \
        *value += step; \
    } \
    *value += result.delta * step * GUI_SPINNER_SENS; \
    *value = max(min(*value, maxValue), minValue); \
    if (oldValue != *value) changed = 1; \
    gui_draw_button(gui, "-", gui_button_state_from_gui(gui, result.decId), result.decBounds); \
    gui_draw_button(gui, "+", gui_button_state_from_gui(gui, result.incId), result.incBounds); \
    return changed; \
}

gui_spinner_def(spinner, int)
gui_spinner_def(spinnerf, float)
gui_spinner_def(spinnerd, double)

#endif

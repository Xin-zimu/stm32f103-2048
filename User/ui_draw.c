#include "ui_draw.h"
#include "bsp_st7789.h"
#include "ui_font.h"

typedef struct
{
    uint8_t *buffer;                    // Active RGB565 high-byte-first strip buffer.
    UI_Rect strip;                      // Screen-space area covered by buffer.
    UI_Rect clip;                       // Active draw clip inside the strip.
    uint8_t active;                     // Nonzero while drawing into a buffer.
} UI_DrawContext;

static UI_DrawContext g_ui_draw_context =
{
    0,
    {0, 0, (int16_t)UI_SCREEN_W, (int16_t)UI_SCREEN_H},
    {0, 0, (int16_t)UI_SCREEN_W, (int16_t)UI_SCREEN_H},
    0U
};

static UI_Rect g_ui_draw_clip =
{
    0,
    0,
    (int16_t)UI_SCREEN_W,
    (int16_t)UI_SCREEN_H
};

/*
 * Clip a rectangle to the screen and active clip.
 *
 * The returned rectangle is safe for either direct ST7789 drawing or strip
 * buffer writes. When a buffer context is active, the rectangle is also clipped
 * to the current strip so row offsets cannot underflow.
 *
 * Parameters:
 * rect: Rectangle to clip in place.
 *
 * Return value:
 * 1: Rectangle remains non-empty.
 * 0: Rectangle is empty or outside the drawable area.
 *
 * Side effects:
 * Modifies rect.
 */
static uint8_t UI_DrawClipRect(UI_Rect *rect)
{
    int16_t right;
    int16_t bottom;
    int16_t clip_right;
    int16_t clip_bottom;
    const UI_Rect *clip;

    if ((rect == 0) || (rect->w <= 0) || (rect->h <= 0) ||
        (rect->x >= (int16_t)UI_SCREEN_W) || (rect->y >= (int16_t)UI_SCREEN_H))
    {
        return 0U;
    }

    if (rect->x < 0)
    {
        rect->w = (int16_t)(rect->w + rect->x);
        rect->x = 0;
    }
    if (rect->y < 0)
    {
        rect->h = (int16_t)(rect->h + rect->y);
        rect->y = 0;
    }
    if ((rect->x + rect->w) > (int16_t)UI_SCREEN_W)
    {
        rect->w = (int16_t)((int16_t)UI_SCREEN_W - rect->x);
    }
    if ((rect->y + rect->h) > (int16_t)UI_SCREEN_H)
    {
        rect->h = (int16_t)((int16_t)UI_SCREEN_H - rect->y);
    }

    clip = (g_ui_draw_context.active != 0U) ? &g_ui_draw_context.clip : &g_ui_draw_clip;
    right = (int16_t)(rect->x + rect->w);
    bottom = (int16_t)(rect->y + rect->h);
    clip_right = (int16_t)(clip->x + clip->w);
    clip_bottom = (int16_t)(clip->y + clip->h);
    if ((right <= clip->x) || (bottom <= clip->y) ||
        (rect->x >= clip_right) || (rect->y >= clip_bottom))
    {
        return 0U;
    }

    if (rect->x < clip->x)
    {
        rect->x = clip->x;
    }
    if (rect->y < clip->y)
    {
        rect->y = clip->y;
    }
    if (right > clip_right)
    {
        right = clip_right;
    }
    if (bottom > clip_bottom)
    {
        bottom = clip_bottom;
    }

    if (g_ui_draw_context.active != 0U)
    {
        const UI_Rect *strip;
        int16_t strip_right;
        int16_t strip_bottom;

        strip = &g_ui_draw_context.strip;
        strip_right = (int16_t)(strip->x + strip->w);
        strip_bottom = (int16_t)(strip->y + strip->h);
        if ((right <= strip->x) || (bottom <= strip->y) ||
            (rect->x >= strip_right) || (rect->y >= strip_bottom))
        {
            return 0U;
        }
        if (rect->x < strip->x)
        {
            rect->x = strip->x;
        }
        if (rect->y < strip->y)
        {
            rect->y = strip->y;
        }
        if (right > strip_right)
        {
            right = strip_right;
        }
        if (bottom > strip_bottom)
        {
            bottom = strip_bottom;
        }
    }

    rect->w = (int16_t)(right - rect->x);
    rect->h = (int16_t)(bottom - rect->y);

    return ((rect->w > 0) && (rect->h > 0)) ? 1U : 0U;
}

/*
 * Fill a clipped rectangle inside the active strip buffer.
 *
 * Renderer strips may cover only the dirty x/w window. Buffer offsets are
 * therefore relative to g_ui_draw_context.strip instead of absolute screen
 * coordinates, otherwise a narrow transfer would send the wrong bytes from
 * the front of the buffer.
 *
 * Parameters:
 * rect: Clipped screen-space rectangle.
 * color: RGB565 fill color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Writes RGB565 high-byte-first pixels into g_ui_draw_context.buffer.
 */
static void UI_DrawRectToBuffer(const UI_Rect *rect, uint16_t color)
{
    int16_t row;
    int16_t col;
    uint16_t offset;
    uint8_t high;
    uint8_t low;

    high = (uint8_t)(color >> 8);
    low = (uint8_t)color;
    for (row = 0; row < rect->h; row++)
    {
        offset = (uint16_t)((((rect->y - g_ui_draw_context.strip.y) + row) *
                             g_ui_draw_context.strip.w +
                             (rect->x - g_ui_draw_context.strip.x)) * 2);
        for (col = 0; col < rect->w; col++)
        {
            g_ui_draw_context.buffer[offset++] = high;
            g_ui_draw_context.buffer[offset++] = low;
        }
    }
}

/*
 * Clear the full visible display.
 *
 * Parameters:
 * color: RGB565 fill color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Replaces the full ST7789 visible area or active buffer context.
 */
void UI_DrawClear(uint16_t color)
{
    if (g_ui_draw_context.active != 0U)
    {
        UI_DrawRect(0, 0, (int16_t)UI_SCREEN_W, (int16_t)UI_SCREEN_H, color);
    }
    else
    {
        ST7789_Clear(color);
    }
}

/*
 * Begin drawing into a renderer-owned strip buffer.
 *
 * Parameters:
 * buffer: RGB565 high-byte-first storage for one strip.
 * strip: Screen-space strip covered by buffer.
 * clip: Drawable area, normally the same as strip.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Replaces the module-local drawing backend until UI_DrawEndBuffer is called.
 */
void UI_DrawBeginBuffer(uint8_t *buffer, const UI_Rect *strip, const UI_Rect *clip)
{
    if ((buffer == 0) || (strip == 0) || (clip == 0))
    {
        return;
    }

    g_ui_draw_context.buffer = buffer;
    g_ui_draw_context.strip = *strip;
    g_ui_draw_context.clip = *clip;
    g_ui_draw_context.active = 1U;
    g_ui_draw_clip = *clip;
}

/*
 * Finish drawing into a renderer-owned strip buffer.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Restores direct ST7789 drawing as the fallback backend.
 */
void UI_DrawEndBuffer(void)
{
    g_ui_draw_context.buffer = 0;
    g_ui_draw_context.active = 0U;
    UI_DrawSetClip(0);
}

/*
 * Set the current drawing clip rectangle.
 *
 * All later UI drawing primitives intersect their target area with this clip.
 * Passing NULL restores a full-screen clip. The clip is intended for dirty
 * rectangle redraw, not for arbitrary nested clipping.
 *
 * Parameters:
 * clip: Rectangle to constrain drawing, or NULL for full screen.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Updates module-local drawing state.
 */
void UI_DrawSetClip(const UI_Rect *clip)
{
    if (clip == 0)
    {
        g_ui_draw_clip.x = 0;
        g_ui_draw_clip.y = 0;
        g_ui_draw_clip.w = (int16_t)UI_SCREEN_W;
        g_ui_draw_clip.h = (int16_t)UI_SCREEN_H;
        if (g_ui_draw_context.active != 0U)
        {
            g_ui_draw_context.clip = g_ui_draw_context.strip;
        }
        return;
    }

    g_ui_draw_clip = *clip;
    if (g_ui_draw_context.active != 0U)
    {
        g_ui_draw_context.clip = *clip;
    }
}

/*
 * Clear only the current clip rectangle.
 *
 * Parameters:
 * color: RGB565 fill color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Repaints the active clipped area.
 */
void UI_DrawClearClip(uint16_t color)
{
    UI_DrawRect(g_ui_draw_clip.x, g_ui_draw_clip.y, g_ui_draw_clip.w, g_ui_draw_clip.h, color);
}

/*
 * Draw a clipped filled rectangle.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * w: Width in pixels.
 * h: Height in pixels.
 * color: RGB565 fill color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws within the active buffer or ST7789 visible area.
 */
void UI_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    UI_Rect rect;

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    if (UI_DrawClipRect(&rect) == 0U)
    {
        return;
    }

    if (g_ui_draw_context.active != 0U)
    {
        UI_DrawRectToBuffer(&rect, color);
    }
    else
    {
        ST7789_FillRect((uint16_t)rect.x, (uint16_t)rect.y, (uint16_t)rect.w, (uint16_t)rect.h, color);
    }
}

/*
 * Draw a one-pixel rectangle frame.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * w: Width in pixels.
 * h: Height in pixels.
 * color: RGB565 frame color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws four clipped rectangle edges.
 */
void UI_DrawFrame(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    UI_DrawRect(x, y, w, 1, color);
    UI_DrawRect(x, (int16_t)(y + h - 1), w, 1, color);
    UI_DrawRect(x, y, 1, h, color);
    UI_DrawRect((int16_t)(x + w - 1), y, 1, h, color);
}

/*
 * Draw one character inside an 8x12 cell.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * ch: ASCII character.
 * color: RGB565 foreground color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws glyph pixels through the clipped rectangle primitive.
 */
static void UI_DrawChar(int16_t x, int16_t y, char ch, uint16_t color)
{
    const uint8_t *glyph;
    uint8_t column;
    uint8_t row;

    glyph = UI_FontGetGlyph(ch);
    for (column = 0U; column < 5U; column++)
    {
        for (row = 0U; row < 7U; row++)
        {
            if ((glyph[column] & (uint8_t)(1U << row)) != 0U)
            {
                UI_DrawRect((int16_t)(x + 1 + column), (int16_t)(y + 2 + row), 1, 1, color);
            }
        }
    }
}

/*
 * Draw one large ASCII character.
 *
 * The large renderer reuses the compact 5x7 glyph table and scales every lit
 * pixel to a 2x2 block. It gives readable numbers and hardware identifiers
 * without adding another full ASCII bitmap table to Flash.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * ch: ASCII character.
 * color: RGB565 foreground color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws scaled glyph pixels through the clipped rectangle primitive.
 */
static void UI_DrawCharLarge(int16_t x, int16_t y, char ch, uint16_t color)
{
    const uint8_t *glyph;
    uint8_t column;
    uint8_t row;

    glyph = UI_FontGetGlyph(ch);
    for (column = 0U; column < 5U; column++)
    {
        for (row = 0U; row < 7U; row++)
        {
            if ((glyph[column] & (uint8_t)(1U << row)) != 0U)
            {
                UI_DrawRect(
                    (int16_t)(x + 1 + ((int16_t)column * 2)),
                    (int16_t)(y + 2 + ((int16_t)row * 2)),
                    2,
                    2,
                    color
                );
            }
        }
    }
}

/*
 * Draw one 16x16 GB2312 Chinese glyph.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * gb2312_code: Two-byte GB2312 code packed as high byte then low byte.
 * color: RGB565 foreground color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws glyph pixels through the clipped rectangle primitive.
 */
static void UI_DrawChineseChar(int16_t x, int16_t y, uint16_t gb2312_code, uint16_t color)
{
    const uint8_t *glyph;
    uint8_t row;
    uint8_t col;
    uint8_t byte_value;

    glyph = UI_FontGetChineseGlyph(gb2312_code);
    if (glyph == 0)
    {
        UI_DrawFrame(x, y, (int16_t)UI_FONT_CN_WIDTH, (int16_t)UI_FONT_CN_HEIGHT, UI_COLOR_DANGER);
        return;
    }

    for (row = 0U; row < UI_FONT_CN_HEIGHT; row++)
    {
        byte_value = glyph[(uint8_t)(row * 2U)];
        for (col = 0U; col < 8U; col++)
        {
            if ((byte_value & (uint8_t)(0x80U >> col)) != 0U)
            {
                UI_DrawRect((int16_t)(x + col), (int16_t)(y + row), 1, 1, color);
            }
        }

        byte_value = glyph[(uint8_t)((row * 2U) + 1U)];
        for (col = 0U; col < 8U; col++)
        {
            if ((byte_value & (uint8_t)(0x80U >> col)) != 0U)
            {
                UI_DrawRect((int16_t)(x + 8 + col), (int16_t)(y + row), 1, 1, color);
            }
        }
    }
}

/*
 * Draw a null-terminated ASCII string.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * text: Null-terminated ASCII text.
 * color: RGB565 text color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws text through the current backend.
 */
void UI_DrawText(int16_t x, int16_t y, const char *text, uint16_t color)
{
    while ((text != 0) && (*text != '\0') && (x < (int16_t)UI_SCREEN_W))
    {
        if (((x + (int16_t)UI_FONT_WIDTH) > g_ui_draw_clip.x) &&
            (x < (g_ui_draw_clip.x + g_ui_draw_clip.w)) &&
            ((y + (int16_t)UI_FONT_HEIGHT) > g_ui_draw_clip.y) &&
            (y < (g_ui_draw_clip.y + g_ui_draw_clip.h)))
        {
            UI_DrawChar(x, y, *text, color);
        }
        x = (int16_t)(x + (int16_t)UI_FONT_WIDTH);
        text++;
    }
}

/*
 * Draw a large ASCII string.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * text: Null-terminated ASCII text.
 * color: RGB565 text color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws scaled ASCII text through the current backend.
 */
void UI_DrawTextLarge(int16_t x, int16_t y, const char *text, uint16_t color)
{
    while ((text != 0) && (*text != '\0') && (x < (int16_t)UI_SCREEN_W))
    {
        if (((x + (int16_t)UI_FONT_LARGE_WIDTH) > g_ui_draw_clip.x) &&
            (x < (g_ui_draw_clip.x + g_ui_draw_clip.w)) &&
            ((y + (int16_t)UI_FONT_LARGE_HEIGHT) > g_ui_draw_clip.y) &&
            (y < (g_ui_draw_clip.y + g_ui_draw_clip.h)))
        {
            UI_DrawCharLarge(x, y, *text, color);
        }
        x = (int16_t)(x + (int16_t)UI_FONT_LARGE_WIDTH);
        text++;
    }
}

/*
 * Draw mixed GB2312 Chinese and ASCII text.
 *
 * Text literals are stored as GB2312 byte sequences. Two bytes with the high
 * bit set are looked up in the Chinese subset table; ASCII bytes are drawn
 * with the large scaled font so model names and values remain readable.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * text: Null-terminated GB2312 or ASCII text.
 * color: RGB565 text color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws mixed text through the current backend.
 */
void UI_DrawTextCN(int16_t x, int16_t y, const char *text, uint16_t color)
{
    const uint8_t *cursor;
    uint16_t code;

    cursor = (const uint8_t *)text;
    while ((cursor != 0) && (*cursor != 0U) && (x < (int16_t)UI_SCREEN_W))
    {
        if ((*cursor >= 0x80U) && (cursor[1] != 0U))
        {
            code = (uint16_t)(((uint16_t)cursor[0] << 8) | cursor[1]);
            if (((x + (int16_t)UI_FONT_CN_WIDTH) > g_ui_draw_clip.x) &&
                (x < (g_ui_draw_clip.x + g_ui_draw_clip.w)) &&
                ((y + (int16_t)UI_FONT_CN_HEIGHT) > g_ui_draw_clip.y) &&
                (y < (g_ui_draw_clip.y + g_ui_draw_clip.h)))
            {
                UI_DrawChineseChar(x, y, code, color);
            }
            x = (int16_t)(x + (int16_t)UI_FONT_CN_WIDTH);
            cursor += 2;
        }
        else
        {
            if (*cursor == ' ')
            {
                x = (int16_t)(x + 8);
            }
            else
            {
                UI_DrawCharLarge(x, y, (char)*cursor, color);
                x = (int16_t)(x + (int16_t)UI_FONT_LARGE_WIDTH);
            }
            cursor++;
        }
    }
}

/*
 * Draw the standard top status bar.
 *
 * Parameters:
 * title: Page title.
 * accent_color: RGB565 bar color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws the top status band.
 */
void UI_DrawStatusBar(const char *title, uint16_t accent_color)
{
    UI_DrawRect(0, 0, (int16_t)UI_SCREEN_W, (int16_t)UI_STATUS_H, accent_color);
    UI_DrawTextCN((int16_t)UI_MARGIN, 8, title, UI_COLOR_BG);
    UI_DrawRect(216, 10, 12, 12, UI_COLOR_BG);
}

/*
 * Draw the standard footer hint band.
 *
 * Parameters:
 * hint: Compact key hint text.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws the bottom hint area.
 */
void UI_DrawFooter(const char *hint)
{
    UI_DrawRect(
        0,
        (int16_t)(UI_SCREEN_H - UI_FOOTER_H),
        (int16_t)UI_SCREEN_W,
        (int16_t)UI_FOOTER_H,
        UI_COLOR_SURFACE
    );
    UI_DrawTextCN((int16_t)UI_MARGIN, 218, hint, UI_COLOR_MUTED);
}

/*
 * Draw a menu or setting row.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * w: Row width.
 * label: Left label text.
 * value: Optional right value text.
 * selected: Nonzero when focused.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws a complete row control.
 */
void UI_DrawMenuRow(
    int16_t x,
    int16_t y,
    int16_t w,
    const char *label,
    const char *value,
    uint8_t selected
)
{
    uint16_t fill;
    uint16_t text;

    fill = (selected != 0U) ? UI_COLOR_SELECTED : UI_COLOR_SURFACE;
    text = (selected != 0U) ? UI_COLOR_BG : UI_COLOR_TEXT;
    UI_DrawRect(x, y, w, (int16_t)UI_ROW_H, fill);
    UI_DrawRect(x, y, 4, (int16_t)UI_ROW_H, UI_COLOR_ACCENT);
    UI_DrawText((int16_t)(x + 14), (int16_t)(y + 12), label, text);
    if (value != 0)
    {
        UI_DrawText((int16_t)(x + w - 70), (int16_t)(y + 12), value, text);
    }
}

/*
 * Draw a large Chinese menu or setting row.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * w: Row width.
 * label: GB2312 left label text.
 * value: Optional GB2312 or ASCII right value text.
 * selected: Nonzero when focused.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws a complete large row control.
 */
void UI_DrawMenuRowCN(
    int16_t x,
    int16_t y,
    int16_t w,
    const char *label,
    const char *value,
    uint8_t selected
)
{
    UI_DrawMenuRowCNEx(x, y, w, label, value, selected, 0U);
}

/*
 * Draw a large Chinese menu or setting row with optional pressed feedback.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * w: Row width.
 * label: GB2312 left label text.
 * value: Optional GB2312 or ASCII right value text.
 * selected: Nonzero when focused.
 * pressed: Nonzero while short press feedback is visible.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws a complete large row control.
 */
void UI_DrawMenuRowCNEx(
    int16_t x,
    int16_t y,
    int16_t w,
    const char *label,
    const char *value,
    uint8_t selected,
    uint8_t pressed
)
{
    uint16_t fill;
    uint16_t text;

    fill = (selected != 0U) ? UI_COLOR_SELECTED : UI_COLOR_SURFACE;
    if (pressed != 0U)
    {
        fill = (selected != 0U) ? UI_COLOR_WARN : UI_COLOR_SURFACE_2;
    }
    text = (selected != 0U) ? UI_COLOR_BG : UI_COLOR_TEXT;
    UI_DrawRect(x, y, w, (int16_t)UI_ROW_H, fill);
    UI_DrawRect(x, (int16_t)(y + UI_ROW_H - 1U), w, 1, UI_COLOR_DIM);
    UI_DrawTextCN((int16_t)(x + 16), (int16_t)(y + 16), label, text);
    if (value != 0)
    {
        UI_DrawTextCN((int16_t)(x + w - 54), (int16_t)(y + 16), value, text);
    }
}

/*
 * Draw a slim animated focus marker beside a menu row.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Marker top coordinate.
 * h: Marker height in pixels.
 * color: RGB565 marker color.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws the focus marker through the clipped rectangle primitive.
 */
void UI_DrawFocusMarker(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    UI_DrawRect(x, (int16_t)(y + 6), 5, (int16_t)(h - 12), color);
}

/*
 * Draw one large information row.
 *
 * Parameters:
 * y: Top coordinate.
 * label: GB2312 left label.
 * value: GB2312 or ASCII value text.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws one clipped information row.
 */
void UI_DrawInfoRowCN(int16_t y, const char *label, const char *value)
{
    UI_DrawRect(12, y, 216, 32, UI_COLOR_SURFACE);
    UI_DrawRect(12, (int16_t)(y + 31), 216, 1, UI_COLOR_DIM);
    UI_DrawTextCN(22, (int16_t)(y + 8), label, UI_COLOR_MUTED);
    UI_DrawTextCN(92, (int16_t)(y + 8), value, UI_COLOR_TEXT);
}

/*
 * Draw a horizontal progress bar.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * w: Width in pixels.
 * value: Current value.
 * max: Maximum value, must be nonzero.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws the progress track and filled region.
 */
void UI_DrawProgressBar(int16_t x, int16_t y, int16_t w, uint8_t value, uint8_t max)
{
    int16_t fill;

    if (max == 0U)
    {
        return;
    }

    fill = (int16_t)(((uint16_t)w * value) / max);
    UI_DrawRect(x, y, w, 10, UI_COLOR_SURFACE_2);
    UI_DrawRect(x, y, fill, 10, UI_COLOR_ACCENT);
    UI_DrawFrame(x, y, w, 10, UI_COLOR_MUTED);
}

/*
 * Draw a compact on/off toggle.
 *
 * Parameters:
 * x: Left coordinate.
 * y: Top coordinate.
 * enabled: Nonzero for ON.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Draws the toggle control.
 */
void UI_DrawToggle(int16_t x, int16_t y, uint8_t enabled)
{
    UI_DrawRect(x, y, 30, 14, (enabled != 0U) ? UI_COLOR_OK : UI_COLOR_SURFACE_2);
    UI_DrawRect(
        (enabled != 0U) ? (int16_t)(x + 17) : (int16_t)(x + 3),
        (int16_t)(y + 3),
        10,
        8,
        UI_COLOR_TEXT
    );
}

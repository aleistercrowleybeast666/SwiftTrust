#include "st_ui.h"
#include <string.h>
static const struct
{
    char code;
    uint8_t columns[5];
} glyphs[] = {{'A', {0x7e, 0x09, 0x09, 0x09, 0x7e}}, {'B', {0x7f, 0x49, 0x49, 0x49, 0x36}},
              {'C', {0x3e, 0x41, 0x41, 0x41, 0x41}}, {'D', {0x7f, 0x41, 0x41, 0x41, 0x3e}},
              {'E', {0x7f, 0x49, 0x49, 0x49, 0x41}}, {'F', {0x7f, 0x09, 0x09, 0x09, 0x01}},
              {'G', {0x3e, 0x41, 0x49, 0x49, 0x79}}, {'H', {0x7f, 0x08, 0x08, 0x08, 0x7f}},
              {'I', {0x41, 0x41, 0x7f, 0x41, 0x41}}, {'J', {0x30, 0x40, 0x41, 0x3f, 0x01}},
              {'K', {0x7f, 0x08, 0x14, 0x22, 0x41}}, {'L', {0x7f, 0x40, 0x40, 0x40, 0x40}},
              {'M', {0x7f, 0x02, 0x0c, 0x02, 0x7f}}, {'N', {0x7f, 0x02, 0x04, 0x08, 0x7f}},
              {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}}, {'P', {0x7f, 0x09, 0x09, 0x09, 0x06}},
              {'Q', {0x3e, 0x41, 0x51, 0x21, 0x5e}}, {'R', {0x7f, 0x09, 0x19, 0x29, 0x46}},
              {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x01, 0x01, 0x7f, 0x01, 0x01}},
              {'U', {0x3f, 0x40, 0x40, 0x40, 0x3f}}, {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
              {'W', {0x7f, 0x20, 0x18, 0x20, 0x7f}}, {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
              {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}}, {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
              {'0', {0x3e, 0x51, 0x49, 0x45, 0x3e}}, {'1', {0x00, 0x42, 0x7f, 0x40, 0x00}},
              {'2', {0x42, 0x61, 0x51, 0x49, 0x46}}, {'3', {0x41, 0x49, 0x49, 0x49, 0x36}},
              {'4', {0x18, 0x14, 0x12, 0x7f, 0x10}}, {'5', {0x4f, 0x49, 0x49, 0x49, 0x31}},
              {'6', {0x3e, 0x49, 0x49, 0x49, 0x30}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
              {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x06, 0x49, 0x49, 0x49, 0x3e}},
              {'+', {0x08, 0x08, 0x3e, 0x08, 0x08}}, {'!', {0x00, 0x00, 0x5f, 0x00, 0x00}},
              {'>', {0x41, 0x22, 0x14, 0x08, 0x00}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
              {'.', {0x00, 0x00, 0x60, 0x60, 0x00}}, {'/', {0x40, 0x30, 0x08, 0x06, 0x01}},
              {'g', {0x18, 0xa4, 0xa4, 0xa4, 0x7c}}, {'k', {0x7f, 0x10, 0x28, 0x44, 0x00}},
              {'m', {0x7c, 0x04, 0x18, 0x04, 0x78}}, {'s', {0x48, 0x54, 0x54, 0x54, 0x20}},
              {'c', {0x38, 0x44, 0x44, 0x44, 0x28}}, {'o', {0x38, 0x44, 0x44, 0x44, 0x38}},
              {'u', {0x3c, 0x40, 0x40, 0x20, 0x7c}}, {'n', {0x7c, 0x08, 0x04, 0x04, 0x78}},
              {'t', {0x04, 0x3f, 0x44, 0x40, 0x20}}, {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
              {':', {0x00, 0x00, 0x24, 0x00, 0x00}}, {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
              {' ', {0x00, 0x00, 0x00, 0x00, 0x00}}};
#if ST_UI_CHINESE
#include "st_font_subset.inc"
#endif
static bool Ui_ReadCodepoint(const char **cursor, uint32_t *value)
{
    const unsigned char *s = (const unsigned char *)*cursor;
    if (!*s)
        return false;
    unsigned count = *s < 0x80                  ? 1
                     : *s >= 0xC2 && *s <= 0xDF ? 2
                     : *s >= 0xE0 && *s <= 0xEF ? 3
                     : *s >= 0xF0 && *s <= 0xF4 ? 4
                                                : 0;
    if (!count)
    {
        *value = UINT32_MAX;
        (*cursor)++;
        return true;
    }
    uint32_t code = count == 1 ? *s : (uint32_t)(*s & ((1u << (7u - count)) - 1u));
    for (unsigned i = 1; i < count; i++)
    {
        if (!s[i] || (s[i] & 0xC0u) != 0x80u)
        {
            *value = UINT32_MAX;
            (*cursor)++;
            return true;
        }
        code = (code << 6) | (s[i] & 0x3Fu);
    }
    if ((count == 2 && code < 0x80) || (count == 3 && code < 0x800) ||
        (count == 4 && code < 0x10000) || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF)
    {
        *value = UINT32_MAX;
        (*cursor)++;
        return true;
    }
    *value = code;
    *cursor += count;
    return true;
}
static const uint8_t *Ui_FindGlyph(uint32_t code, unsigned *width)
{
    *width = 6;
    if (code < 128)
    {
        for (unsigned i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++)
            if ((uint32_t)(unsigned char)glyphs[i].code == code)
                return glyphs[i].columns;
        return NULL;
    }
#if ST_UI_CHINESE
    *width = 12;
    for (unsigned i = 0; i < sizeof cjk_glyphs / sizeof cjk_glyphs[0]; i++)
        if (cjk_glyphs[i].code == code)
            return cjk_glyphs[i].columns;
#endif
    return NULL;
}
UiTextResult Ui_ValidateText(const char *text, unsigned *pixels)
{
    if (!text || !pixels)
        return UI_TEXT_INVALID;
    *pixels = 0;
    uint32_t code;
    while (Ui_ReadCodepoint(&text, &code))
    {
        if (code == UINT32_MAX)
            return UI_TEXT_INVALID;
        unsigned width;
        const uint8_t *glyph = Ui_FindGlyph(code, &width);
        if (!glyph)
            return UI_TEXT_MISSING_GLYPH;
        *pixels += width;
    }
    return *pixels > ST_OLED_WIDTH ? UI_TEXT_TOO_WIDE : UI_TEXT_OK;
}
static void Ui_SetPixel(uint8_t frame[1024], unsigned x, unsigned y)
{
    if (x < ST_OLED_WIDTH && y < ST_OLED_HEIGHT)
        frame[(y / 8u) * ST_OLED_WIDTH + x] |= (uint8_t)(1u << (y % 8u));
}
void st_ui_raster(const st_ui_model *model, uint8_t frame[1024])
{
    memset(frame, 0, 1024);
    for (unsigned row = 0; row < ST_OLED_ROWS; row++)
    {
        const char *text = model->rows[row];
        unsigned x = 0;
        uint32_t code;
        while (Ui_ReadCodepoint(&text, &code))
        {
            unsigned width;
            const uint8_t *glyph = Ui_FindGlyph(code, &width);
            if (!glyph)
            {
                code = '?';
                glyph = Ui_FindGlyph(code, &width);
            }
            if (x + width > ST_OLED_WIDTH)
                break;
            if (glyph)
            {
                for (unsigned column = 0; column < (code < 128 ? 5u : 12u); column++)
                    for (unsigned y = 0; y < (code < 128 ? 12u : 14u); y++)
                    {
                        bool on = code < 128 ? ((glyph[column] >> (y * 7u / 12u)) & 1u) != 0
                                             : (((unsigned)glyph[column * 2u] |
                                                 ((unsigned)glyph[column * 2u + 1u] << 8)) &
                                                (1u << y)) != 0;
                        if (on)
                            Ui_SetPixel(frame, x + column,
                                        row * ST_OLED_ROW_HEIGHT + (code < 128 ? 2u : 1u) + y);
                    }
            }
            x += width;
        }
    }
    if (model->inverse && model->inverse_row < ST_OLED_ROWS)
    {
        unsigned start = model->inverse_digit ? (unsigned)model->inverse_column * 6u : 0;
        unsigned end = model->inverse_digit ? start + 6u : ST_OLED_WIDTH;
        if (end > ST_OLED_WIDTH)
            end = ST_OLED_WIDTH;
        for (unsigned x = start; x < end; x++)
            for (unsigned y = 0; y < ST_OLED_ROW_HEIGHT; y++)
                frame[((unsigned)model->inverse_row * 2u + y / 8u) * ST_OLED_WIDTH + x] ^=
                    (uint8_t)(1u << (y % 8u));
    }
}

#include "raster.h"
#include "raster_internal.h"

static uint8_t FillPatterns[][8] =
{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // EMPTY_FILL
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, // SOLID_FILL
    {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00}, // LINE_FILL
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}, // LTSLASH_FILL
    {0x83, 0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0, 0xc1}, // SLASH_FILL
    {0xf0, 0x78, 0x3c, 0x1e, 0x0f, 0x87, 0xc3, 0xe1}, // BKSLASH_FILL
    {0xa5, 0xd2, 0x69, 0xb4, 0x5a, 0x2d, 0x96, 0x4b}, // LTBKSLASH_FILL
    {0xff, 0x88, 0x88, 0x88, 0xff, 0x88, 0x88, 0x88}, // HATCH_FILL
    {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}, // XHATCH_FILL
    {0xcc, 0x33, 0xcc, 0x33, 0xcc, 0x33, 0xcc, 0x33}, // INTERLEAVE_FILL
    {0x08, 0x00, 0x80, 0x00, 0x08, 0x00, 0x80, 0x00}, // WIDE_DOT_FILL
    {0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00}  // CLOSE_DOT_FILL
};

static uint16_t LineStyles[] = { 0xffff, 0x3333, 0x1e3f, 0x1f1f };

#define GLYPH_WIDTH  8
#define GLYPH_HEIGHT 8

static const uint64_t RasterFont8x8[] = {
    0x0000000000000000ULL,
    0x7E81A5819DB9817EULL,
    0x7EFFDBFFE3C7FF7EULL,
    0x6CFEFEFE7C381000ULL,
    0x10387CFE7C381000ULL,
    0x387C38FEFE10107CULL,
    0x00183C7EFF7E187EULL,
    0x0000183C3C180000ULL,
    0xFFFFE7C3C3E7FFFFULL,
    0x003C664242663C00ULL,
    0xFFC399BDBD99C3FFULL,
    0x0F070F7DCCCCCC78ULL,
    0x3C6666663C187E18ULL,
    0x3F333F303070F0E0ULL,
    0x7F637F636367E6C0ULL,
    0x995A3CE7E73C5A99ULL,
    0x80E0F8FEF8E08000ULL,
    0x020E3EFE3E0E0200ULL,
    0x183C7E18187E3C18ULL,
    0x6666666666006600ULL,
    0x7FDBDB7B1B1B1B00ULL,
    0x3F607C66663E06FCULL,
    0x000000007E7E7E00ULL,
    0x183C7E187E3C18FFULL,
    0x183C7E1818181800ULL,
    0x181818187E3C1800ULL,
    0x00180CFE0C180000ULL,
    0x003060FE60300000ULL,
    0x0000C0C0C0FE0000ULL,
    0x002466FF66240000ULL,
    0x00183C7EFFFF0000ULL,
    0x00FFFF7E3C180000ULL,
    0x0000000000000000ULL,
    0x1818181818001800ULL,
    0x6C6C6C0000000000ULL,
    0x6C6CFE6CFE6C6C00ULL,
    0x187EC07C06FC1800ULL,
    0x00C6CC183066C600ULL,
    0x386C3876DCCC7600ULL,
    0x3030600000000000ULL,
    0x0C18303030180C00ULL,
    0x30180C0C0C183000ULL,
    0x00663CFF3C660000ULL,
    0x0018187E18180000ULL,
    0x0000000000181830ULL,
    0x0000007E00000000ULL,
    0x0000000000181800ULL,
    0x060C183060C08000ULL,
    0x7CCEDEF6E6C67C00ULL,
    0x1838181818187E00ULL,
    0x7CC6067CC0C0FE00ULL,
    0xFC06063C0606FC00ULL,
    0x0CCCCCCCFE0C0C00ULL,
    0xFEC0FC0606C67C00ULL,
    0x7CC0C0FCC6C67C00ULL,
    0xFE06060C18303000ULL,
    0x7CC6C67CC6C67C00ULL,
    0x7CC6C67E06067C00ULL,
    0x0018180000181800ULL,
    0x0018180000181830ULL,
    0x0C18306030180C00ULL,
    0x00007E007E000000ULL,
    0x30180C060C183000ULL,
    0x3C660C1818001800ULL,
    0x7CC6DEDEDEC07E00ULL,
    0x386CC6C6FEC6C600ULL,
    0xFCC6C6FCC6C6FC00ULL,
    0x7CC6C0C0C0C67C00ULL,
    0xF8CCC6C6C6CCF800ULL,
    0xFEC0C0F8C0C0FE00ULL,
    0xFEC0C0F8C0C0C000ULL,
    0x7CC6C0C0CEC67C00ULL,
    0xC6C6C6FEC6C6C600ULL,
    0x7E18181818187E00ULL,
    0x0606060606C67C00ULL,
    0xC6CCD8F0D8CCC600ULL,
    0xC0C0C0C0C0C0FE00ULL,
    0xC6EEFEFED6C6C600ULL,
    0xC6E6F6DECEC6C600ULL,
    0x7CC6C6C6C6C67C00ULL,
    0xFCC6C6FCC0C0C000ULL,
    0x7CC6C6C6D6DE7C06ULL,
    0xFCC6C6FCD8CCC600ULL,
    0x7CC6C07C06C67C00ULL,
    0xFF18181818181800ULL,
    0xC6C6C6C6C6C6FE00ULL,
    0xC6C6C6C6C67C3800ULL,
    0xC6C6C6C6D6FE6C00ULL,
    0xC6C66C386CC6C600ULL,
    0xC6C6C67C1830E000ULL,
    0xFE060C183060FE00ULL,
    0x3C30303030303C00ULL,
    0xC06030180C060200ULL,
    0x3C0C0C0C0C0C3C00ULL,
    0x10386CC600000000ULL,
    0x00000000000000FFULL,
    0x18180C0000000000ULL,
    0x00007C067EC67E00ULL,
    0xC0C0C0FCC6C6FC00ULL,
    0x00007CC6C0C67C00ULL,
    0x0606067EC6C67E00ULL,
    0x00007CC6FEC07C00ULL,
    0x1C36307830307800ULL,
    0x00007EC6C67E06FCULL,
    0xC0C0FCC6C6C6C600ULL,
    0x1800381818183C00ULL,
    0x060006060606C67CULL,
    0xC0C0CCD8F8CCC600ULL,
    0x3818181818183C00ULL,
    0x0000CCFEFED6D600ULL,
    0x0000FCC6C6C6C600ULL,
    0x00007CC6C6C67C00ULL,
    0x0000FCC6C6FCC0C0ULL,
    0x00007EC6C67E0606ULL,
    0x0000FCC6C0C0C000ULL,
    0x00007EC07C06FC00ULL,
    0x18187E1818180E00ULL,
    0x0000C6C6C6C67E00ULL,
    0x0000C6C6C67C3800ULL,
    0x0000C6C6D6FE6C00ULL,
    0x0000C66C386CC600ULL,
    0x0000C6C6C67E06FCULL,
    0x0000FE0C3860FE00ULL,
    0x0E18187018180E00ULL,
    0x1818180018181800ULL,
    0x7018180E18187000ULL,
    0x76DC000000000000ULL,
    0x0010386CC6C6FE00ULL,
    0x7CC6C0C0C0D67C30ULL,
    0xC600C6C6C6C67E00ULL,
    0x0E007CC6FEC07C00ULL,
    0x7E813C067EC67E00ULL,
    0x66007C067EC67E00ULL,
    0xE0007C067EC67E00ULL,
    0x18187C067EC67E00ULL,
    0x00007CC6C0D67C30ULL,
    0x7E817CC6FEC07C00ULL,
    0x66007CC6FEC07C00ULL,
    0xE0007CC6FEC07C00ULL,
    0x6600381818183C00ULL,
    0x7C82381818183C00ULL,
    0x7000381818183C00ULL,
    0xC6107CC6FEC6C600ULL,
    0x3838007CC6FEC600ULL,
    0x0E00FEC0F8C0FE00ULL,
    0x00007F0C7FCC7F00ULL,
    0x3F6CCCFFCCCCCF00ULL,
    0x7C827CC6C6C67C00ULL,
    0x66007CC6C6C67C00ULL,
    0xE0007CC6C6C67C00ULL,
    0x7C8200C6C6C67E00ULL,
    0xE000C6C6C6C67E00ULL,
    0x66006666663E067CULL,
    0xC67CC6C6C6C67C00ULL,
    0xC600C6C6C6C6FE00ULL,
    0x18187ED8D8D87E18ULL,
    0x386C60F06066FC00ULL,
    0x66663C187E187E18ULL,
    0xF8CCCCFAC6CFC6C3ULL,
    0x0E1B183C1818D870ULL,
    0x0E007C067EC67E00ULL,
    0x1C00381818183C00ULL,
    0x0E007CC6C6C67C00ULL,
    0x0E00C6C6C6C67E00ULL,
    0x00FE00FCC6C6C600ULL,
    0xFE00C6E6F6DECE00ULL,
    0x3C6C6C3E007E0000ULL,
    0x3C66663C007E0000ULL,
    0x1800181830663C00ULL,
    0x000000FCC0C00000ULL,
    0x000000FC0C0C0000ULL,
    0xC6CCD83F63CF8C0FULL,
    0xC3C6CCDB376DCF03ULL,
    0x1800181818181800ULL,
    0x003366CC66330000ULL,
    0x00CC663366CC0000ULL,
    0x2288228822882288ULL,
    0x55AA55AA55AA55AAULL,
    0xDD77DD77DD77DD77ULL,
    0x1818181818181818ULL,
    0x18181818F8181818ULL,
    0x1818F818F8181818ULL,
    0x36363636F6363636ULL,
    0x00000000FE363636ULL,
    0x0000F818F8181818ULL,
    0x3636F606F6363636ULL,
    0x3636363636363636ULL,
    0x0000FE06F6363636ULL,
    0x3636F606FE000000ULL,
    0x36363636FE000000ULL,
    0x1818F818F8000000ULL,
    0x00000000F8181818ULL,
    0x181818181F000000ULL,
    0x18181818FF000000ULL,
    0x00000000FF181818ULL,
    0x181818181F181818ULL,
    0x00000000FF000000ULL,
    0x18181818FF181818ULL,
    0x18181F181F181818ULL,
    0x3636363637363636ULL,
    0x363637303F000000ULL,
    0x00003F3037363636ULL,
    0x3636F700FF000000ULL,
    0x0000FF00F7363636ULL,
    0x3636373037363636ULL,
    0x0000FF00FF000000ULL,
    0x3636F700F7363636ULL,
    0x1818FF00FF000000ULL,
    0x36363636FF000000ULL,
    0x0000FF00FF181818ULL,
    0x00000000FF363636ULL,
    0x363636363F000000ULL,
    0x18181F181F000000ULL,
    0x00001F181F181818ULL,
    0x000000003F363636ULL,
    0x36363636FF363636ULL,
    0x1818FF18FF181818ULL,
    0x18181818F8000000ULL,
    0x000000001F181818ULL,
    0xFFFFFFFFFFFFFFFFULL,
    0x00000000FFFFFFFFULL,
    0xF0F0F0F0F0F0F0F0ULL,
    0x0F0F0F0F0F0F0F0FULL,
    0xFFFFFFFF00000000ULL,
    0x000076DCC8DC7600ULL,
    0x386C6C786C666C60ULL,
    0x00FEC6C0C0C0C000ULL,
    0x0000FE6C6C6C6C00ULL,
    0xFE6030183060FE00ULL,
    0x00007ED8D8D87000ULL,
    0x00666666667C60C0ULL,
    0x0076DC1818181800ULL,
    0x7E183C66663C187EULL,
    0x3C66C3FFC3663C00ULL,
    0x3C66C3C36666E700ULL,
    0x0E180C7EC6C67C00ULL,
    0x00007EDBDB7E0000ULL,
    0x060C7EDBDB7E60C0ULL,
    0x3860C0F8C0603800ULL,
    0x78CCCCCCCCCCCC00ULL,
    0x007E007E007E0000ULL,
    0x18187E1818007E00ULL,
    0x603018306000FC00ULL,
    0x183060301800FC00ULL,
    0x0E1B1B1818181818ULL,
    0x1818181818D8D870ULL,
    0x1818007E00181800ULL,
    0x0076DC0076DC0000ULL,
    0x386C6C3800000000ULL,
    0x0000001818000000ULL,
    0x0000000018000000ULL,
    0x0F0C0C0CEC6C3C1CULL,
    0x786C6C6C6C000000ULL,
    0x7C0C7C607C000000ULL,
    0x00003C3C3C3C0000ULL,
    0x0010000000000000ULL,
};

static void DrawPixelToScreenSafe(int x, int y, rgba8 color)
{
    if (x < 0 || x > FB_WIDTH - 1 || y < 0 || y > FB_HEIGHT - 1) {
        return;
    }
    DrawPixelToScreen(x, y, color);
}

void DrawLine(int x0, int y0, int x1, int y1, rgba8 color, uint8_t thickness /*= 1*/, LineStyle style /*= SOLID_LINE */)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx, sy;

    if (dx > 0)      sx = 1;
    else if (dx < 0) sx = -1, dx = -dx;
    else             sx = 0;

    if (dy > 0)      sy = 1;
    else if (dy < 0) sy = -1, dy = -dy;
    else             sy = 0;

    int ax = 2 * dx, ay = 2 * dy;
    int range = thickness / 2;
    int maskIndex = 0;
    uint16_t mask = LineStyles[style];

    if (dy <= dx)
    {
        for (int decy = ay - dx; ; x0 += sx, decy += ay)
        {
            if ((mask >> (15 - maskIndex)) & 1)
            {
                for (int offset = -range; offset <= range; ++offset) {
                    DrawPixelToScreenSafe(x0, y0 + offset, color);
                }
            }

            maskIndex = (maskIndex + 1) % 16;

            if (x0 == x1) {
                break;
            }

            if (decy >= 0)
            {
                decy -= ax;
                y0 += sy;
            }
        }
    }
    else
    {
        for (int decx = ax - dy; ; y0 += sy, decx += ax)
        {
            if ((mask >> (15 - maskIndex)) & 1)
            {
                for (int offset = -range; offset <= range; ++offset) {
                    DrawPixelToScreenSafe(x0 + offset, y0, color);
                }
            }

            maskIndex = (maskIndex + 1) % 16;

            if (y0 == y1) {
                break;
            }

            if (decx >= 0)
            {
                decx -= ay;
                x0 += sx;
            }
        }
    }
}

static void FillHorizontalLine(int x1, int x2, int y, uint32_t color, uint8_t style[8])
{
    uint8_t mask = style[y & 7];

    for (int x = x1; x <= x2; x++)
    {
        if (mask & (1 << (7 - (x & 7)))) {
            DrawPixelToScreenSafe(x, y, color);
        }
    }
}

void DrawRectangle(int posX, int posY, int w, int h, rgba8 color, FillStyle style)
{
    if (w <= 0 || h <= 0) return;

    for (int x = posX; x < posX + w; x++) 
    {
        DrawPixelToScreenSafe(x, posY, color);
        DrawPixelToScreenSafe(x, posY + h - 1, color);
    }

    for (int y = posY; y < posY + h; y++) {
        DrawPixelToScreenSafe(posX, y, color);
        DrawPixelToScreenSafe(posX + w - 1, y, color);
    }

    for (int y = 0; y < h; y++) {
        FillHorizontalLine(posX, posX + w, posY + y, color, FillPatterns[style]);
    }
}

void DrawCircle(int cx, int cy, int radius, uint32_t color, FillStyle style)
{
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    while (x <= y)
    {
        DrawPixelToScreenSafe(cx + x, cy + y, color);
        DrawPixelToScreenSafe(cx - x, cy + y, color);
        DrawPixelToScreenSafe(cx + x, cy - y, color);
        DrawPixelToScreenSafe(cx - x, cy - y, color);
        DrawPixelToScreenSafe(cx + y, cy + x, color);
        DrawPixelToScreenSafe(cx - y, cy + x, color);
        DrawPixelToScreenSafe(cx + y, cy - x, color);
        DrawPixelToScreenSafe(cx - y, cy - x, color);

        FillHorizontalLine(cx - x, cx + x, cy + y, color, FillPatterns[style]);
        FillHorizontalLine(cx - x, cx + x, cy - y, color, FillPatterns[style]);
        FillHorizontalLine(cx - y, cx + y, cy + x, color, FillPatterns[style]);
        FillHorizontalLine(cx - y, cx + y, cy - x, color, FillPatterns[style]);

        if (d >= 0) {
            d += -4 * (y--) + 4;
        }

        d += 4 * x + 6;

        x++;
    }
}

void DrawEllipse(int cx, int cy, int rx, int ry, uint32_t color, FillStyle style)
{
    int x = 0, y = ry;
    long rx2 = rx * rx, ry2 = ry * ry;
    long twoRx2 = 2 * rx2, twoRy2 = 2 * ry2;
    long px = 0, py = twoRx2 * y;

    long p = ry2 - (rx2 * ry) + (rx2 / 4);
    while (px < py)
    {
        FillHorizontalLine(cx - x, cx + x, cy + y, color, FillPatterns[style]);
        FillHorizontalLine(cx - x, cx + x, cy - y, color, FillPatterns[style]);

        DrawPixelToScreenSafe(cx + x, cy + y, color);
        DrawPixelToScreenSafe(cx - x, cy + y, color);
        DrawPixelToScreenSafe(cx + x, cy - y, color);
        DrawPixelToScreenSafe(cx - x, cy - y, color);

        x++;
        px += twoRy2;
        if (p < 0) {
            p += ry2 + px;
        }
        else {
            y--;
            py -= twoRx2;
            p += ry2 + px - py;
        }
    }

    p = (long)(ry2 * (x + 0.5f) * (x + 0.5f) + rx2 * (y - 1) * (y - 1) - rx2 * ry2);
    while (y >= 0)
    {
        FillHorizontalLine(cx - x, cx + x, cy + y, color, FillPatterns[style]);
        FillHorizontalLine(cx - x, cx + x, cy - y, color, FillPatterns[style]);

        DrawPixelToScreenSafe(cx + x, cy + y, color);
        DrawPixelToScreenSafe(cx - x, cy + y, color);
        DrawPixelToScreenSafe(cx + x, cy - y, color);
        DrawPixelToScreenSafe(cx - x, cy - y, color);

        y--;
        py -= twoRx2;
        if (p > 0) {
            p += rx2 - py;
        }
        else
        {
            x++;
            px += twoRy2;
            p += rx2 - py + px;
        }
    }
}

static void WriteChar(int posX, int posY, char c, uint32_t color)
{
    uint64_t glyph = RasterFont8x8[(unsigned char)c];

    for (int y = 0; y < GLYPH_HEIGHT; y++)
    {
        uint8_t bits = (glyph >> ((7 - y) * 8)) & 0xFF;

        for (int x = 0; x < 8; x++)
        {
            if (bits & (1 << (7 - x))) {
                DrawPixelToScreenSafe(posX + x, posY + y, color);
            }
        }
    }
}

void WriteString(const char* text, int posX, int posY, uint32_t color)
{
    size_t length = strlen(text);
    for (int i = 0; i < length; ++i) {
        WriteChar(posX + i * GLYPH_WIDTH, posY, text[i], color);
    }
}
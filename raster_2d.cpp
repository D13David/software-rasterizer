#include "raster.h"

void DrawLine(int x0, int y0, int x1, int y1, Color Color)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (1)
    {
        DrawPixelToScreen(x0, y0, Color);

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }

        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void DrawRectangle(int posX, int posY, int w, int h, Color color)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            DrawPixelToScreen(posX + x, posY + y, color);
        }
    }
}

uint8_t patterns[][8] = 
{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // EMPTY_FILL
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, // SOLID_FILL
    {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00}, // LINE_FILL
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}, // LTSLASH_FILL
    {0x0f, 0x3c, 0xf0, 0xc3, 0x0f, 0x3c, 0xf0, 0xc3}, // SLASH_FILL
    // BKSLASH_FILL
    // LTBKSLASH_FILL
    // HATCH_FILL
    {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}, // XHATCH_FILL
    {0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55}, // INTERLEAVE_FILL
    // WIDE_DOT_FILL
    // CLOSE_DOT_FILL
};

static void drawHLine(int x1, int x2, int y, uint32_t color, int style) {
    int patY = y & 7;  // pattern row (wrap every 8)
    uint8_t mask = patterns[style][patY];

    for (int x = x1; x <= x2; x++) {
        int bit = 1 << (7 - (x & 7));  // pattern column (wrap every 8)
        if (mask & bit)
            DrawPixelToScreen(x, y, color);
    }
}

void DrawEllipseFilled(int cx, int cy, int rx, int ry, uint32_t color, int style) 
{
    int x = 0, y = ry;
    long rx2 = rx * rx, ry2 = ry * ry;
    long twoRx2 = 2 * rx2, twoRy2 = 2 * ry2;
    long px = 0, py = twoRx2 * y;

    long p = ry2 - (rx2 * ry) + (rx2 / 4);
    while (px < py) {
        drawHLine(cx - x, cx + x, cy + y, color, style);
        drawHLine(cx - x, cx + x, cy - y, color, style);

        DrawPixelToScreen(cx + x, cy + y, color);
        DrawPixelToScreen(cx - x, cy + y, color);
        DrawPixelToScreen(cx + x, cy - y, color);
        DrawPixelToScreen(cx - x, cy - y, color);

        x++;
        px += twoRy2;
        if (p < 0)
            p += ry2 + px;
        else {
            y--;
            py -= twoRx2;
            p += ry2 + px - py;
        }
    }

    p = ry2 * (x + 0.5) * (x + 0.5) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        drawHLine(cx - x, cx + x, cy + y, color, style);
        drawHLine(cx - x, cx + x, cy - y, color, style);

        DrawPixelToScreen(cx + x, cy + y, color);
        DrawPixelToScreen(cx - x, cy + y, color);
        DrawPixelToScreen(cx + x, cy - y, color);
        DrawPixelToScreen(cx - x, cy - y, color);

        y--;
        py -= twoRx2;
        if (p > 0)
            p += rx2 - py;
        else {
            x++;
            px += twoRy2;
            p += rx2 - py + px;
        }
    }
}

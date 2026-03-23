#include "fps_meter.h"
#include "raster.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#define MAX_HISTORY_ITEMS 64
#define FPS_MAX 90
#define UPDATE_FREQUENCY 300

struct FpsMeterContext
{
    DWORD time;
    int frames;
    int history[MAX_HISTORY_ITEMS];
    int head;
    int size;
};

FpsMeterContext context;

void FPSMeterInitialize()
{
    context.time = GetTickCount();
    context.frames = 0;
    context.head = 0;
    context.size = MAX_HISTORY_ITEMS;
}

void FPSMeterUpdate()
{
    context.frames++;

    DWORD currentTime = GetTickCount();
    DWORD deltaTime = currentTime - context.time;

    if (deltaTime > UPDATE_FREQUENCY)
    {
        float fps = (context.frames * 1000.0f) / deltaTime;
        context.history[context.head] = (int)(fps + 0.5f);
        context.head = (context.head + 1) % MAX_HISTORY_ITEMS;
        context.frames = 0;
        context.time = currentTime;
    }
}

void FPSMeterDraw(int x, int y, int width, int height)
{
    int offsetY = y + height;

    int markers[] = { 30, 60, 90 };
    for (int i = 0; i < 3; ++i)
    {
        int posY = markers[i] * height / FPS_MAX;
        DrawLine(x, offsetY - posY, width, offsetY - posY, COLOR(1, 1, 1), 1, LineStyle::DOTTED_LINE);
        WriteString(Format("%d", markers[i]), x + width + 8, offsetY - posY - 4);
    }

    if (context.size < 2) {
        return;
    }

    int steps = MAX_HISTORY_ITEMS - 1;
    int startIndex = (context.head - context.size + MAX_HISTORY_ITEMS) % MAX_HISTORY_ITEMS;
    int currentX = x;
    int error = 0;

    for (int i = 0; i < context.size - 1; ++i)
    {
        int nextX = currentX + width / steps;
        error += width % steps;
        if (error >= steps)
        {
            nextX++;
            error -= steps;
        }

        int value0 = context.history[(startIndex + i) % MAX_HISTORY_ITEMS];
        int value1 = context.history[(startIndex + i + 1) % MAX_HISTORY_ITEMS];

        if (value0 > FPS_MAX) value0 = FPS_MAX;
        if (value1 > FPS_MAX) value1 = FPS_MAX;

        DrawLine(
                    currentX, offsetY - (value0 * height) / FPS_MAX,
                    nextX,    offsetY - (value1 * height) / FPS_MAX,
                    value1 < 30 ? COLOR(1, 0, 0) : COLOR(0, 1, 0), 3
                );

        currentX = nextX;
    }
}

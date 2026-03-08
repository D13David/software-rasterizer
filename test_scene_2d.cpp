#include "shared.h"
#include "profile.h"
#include "raster.h"
#include "thirteen.h"

#if PJD_ACTIVE_SCENE == TEST_SCENE_2D

enum SceneState
{
    StateFillEllipse,
    StateRandBar,
    StateBar,
    StateLineStyle,

    StateMax
};

SceneState State = (SceneState)(StateMax - 1);

static void NextState()
{
    State = (SceneState)(State + 1);
    if (State >= StateMax) {
        State = (SceneState)0;
    }

    Clear(0);
    ResolveFrameBuffer();
}

static rgba8 RandColor()
{
    return RGB(RandomRange(0, 255), RandomRange(0, 255), RandomRange(0, 255));
}

static FillStyle RandFillStyle()
{
    return (FillStyle)RandomRange(EMPTY_FILL, CLOSE_DOT_FILL + 1);
}

bool SceneInitialize()
{
    NextState();

    return true;
}

void SceneDestroy()
{
}

void SceneRenderFrame()
{
}

static bool FillEllipsePlay()
{
    int MaxRadius = FB_HEIGHT / 10;

    DrawEllipse(RandomRange(0, FB_WIDTH - 1), RandomRange(0, FB_HEIGHT),
        RandomRange(0, MaxRadius), RandomRange(0, MaxRadius),
        RandColor(), RandFillStyle());

    return true;
}

static bool LineStylePlay()
{
    int x = 35;
    int y = 10;
    int step = (FB_WIDTH) / 11;

    for (int style = 0; style <= 3; ++style, x += step) {
        DrawLine(x, y + 20, x, FB_HEIGHT - 80, COLOR(1, 1, 1), 1, (LineStyle)style);
    }

    x += 2 * step;

    for (int style = 0; style <= 3; ++style, x += step) {
        DrawLine(x, y + 20, x, FB_HEIGHT - 80, COLOR(1, 1, 1), 3, (LineStyle)style);
    }

    return false;
}

static bool RandBarPlay()
{
    rgba8 color = RandColor();
    int x = RandomRange(0, FB_WIDTH - 1);
    int y = RandomRange(0, FB_WIDTH - 1);
    DrawRectangle(x, y, RandomRange(0, FB_WIDTH - 1) - x, RandomRange(0, FB_HEIGHT - 1) - y, 
        color, (FillStyle)RandomRange(EMPTY_FILL, CLOSE_DOT_FILL+1));
    return true;
}

static bool BarPlay()
{
    const int NumBars = 5;
    const int BarHeight[] = { 1, 3, 5, 2, 4 };
    const int BarStyles[] = { 1, 3, 10, 5, 9 };
    const int H = 15;

    const int YStep = (FB_HEIGHT - (2 * H)) / NumBars;
    const int XStep = (FB_WIDTH - (2 * H)) / NumBars;

    int x = H;
    for (int i = 0; i < NumBars; ++i)
    {
        DrawRectangle(x, FB_HEIGHT - H - (BarHeight[i] * YStep), XStep, (BarHeight[i] * YStep),
            RandColor(), RandFillStyle());

        x += XStep;
    }
    return false;
}

typedef bool (*StateHandler)();

static StateHandler StateHandlers[] =
{
    &FillEllipsePlay,
    &RandBarPlay,
    &BarPlay,
    &LineStylePlay,
};

void SceneRenderOverlay2D()
{
    PROFILE_AUTO("Frame");

    static bool refreshState = true;
    
    if (refreshState) {
        refreshState = StateHandlers[State]();
    }

    if (!Thirteen::GetKeyLastFrame(VK_SPACE) && Thirteen::GetKey(VK_SPACE)) 
    {
        refreshState = true;
        NextState();
    }
}

#endif // TEST_SCENE_EMPTY
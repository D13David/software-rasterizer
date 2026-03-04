#include "shared.h"
#include "profile.h"
#include "raster.h"

#if PJD_ACTIVE_SCENE == TEST_SCENE_2D

bool SceneInitialize()
{
    return true;
}

void SceneDestroy()
{
}

void SceneRenderFrame()
{
}

void SceneRenderOverlay2D()
{
    PROFILE_AUTO("Frame");

    Clear(0);

    for (int i = EMPTY_FILL; i <= CLOSE_DOT_FILL; ++i)
    {
        DrawEllipseFilled((FB_WIDTH - 110 * (CLOSE_DOT_FILL - EMPTY_FILL)) / 2 + i * 110,
                           FB_HEIGHT / 2, 50, 50, COLOR(0, 1, 0), (FillStyle)i);
    }
}

#endif // TEST_SCENE_EMPTY
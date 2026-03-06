#ifndef PJD_SHARED_H

#define TEST_SCENE_EMPTY    0
#define TEST_SCENE_UNREAL   1
#define TEST_SCENE_2D       2
#define TEST_SCENE_SPONZA   3

// change this to change scene
#define PJD_ACTIVE_SCENE TEST_SCENE_SPONZA

#if __cplusplus
extern "C" {
#endif

extern bool WireFrameOverlay;
extern float DeltaTime;

#define FB_WIDTH 1920
#define FB_HEIGHT 1080

#if __cplusplus
}
#endif

#endif // PJD_SHARED_H
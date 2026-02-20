#ifndef PJD_PROFILE_H
#define PJD_PROFILE_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if PJD_PROFILING_ENABLED
#define DECLARE_INTERFACE(function) function
#else
#define DECLARE_INTERFACE(function) static PJD_INLINE function {}
#endif

DECLARE_INTERFACE(void ProfilerInitialize());
DECLARE_INTERFACE(void ProfilerReset());
DECLARE_INTERFACE(void DrawProfilerStats(int posX, int posY, int width));
DECLARE_INTERFACE(int ProfileRegisterNode(const char* name));
DECLARE_INTERFACE(void ProfileStackPushId(int id));
DECLARE_INTERFACE(void ProfileStackPop());

#if PJD_PROFILING_ENABLED
#define PROFILE_AUTO(name) \
    static size_t CONCAT(_ProfileId, __LINE__) = (size_t)-1; \
    if (CONCAT(_ProfileId, __LINE__) == (size_t)-1) \
        CONCAT(_ProfileId, __LINE__) = ProfileRegisterNode(name); \
    ProfileStackPushId(CONCAT(_ProfileId, __LINE__)); \
    auto CONCAT(_profile_guard_, __LINE__) = Scoped([](){ ProfileStackPop(); })

#define PROFILE_START(name) \
    static int CONCAT(_ProfileId, __LINE__) = -1; \
    if (CONCAT(_ProfileId, __LINE__) == -1) \
        CONCAT(_ProfileId, __LINE__) = ProfileRegisterNode(name); \
    ProfileStackPushId(CONCAT(_ProfileId, __LINE__))

#define PROFILE_END() ProfileStackPop()
#else
#define PROFILE_AUTO(name)
#define PROFILE_START(name)
#define PROFILE_END()
#endif // PJD_PROFILING_ENABLED

#ifdef __cplusplus
}
#endif

#endif // PJD_PROFILE_H
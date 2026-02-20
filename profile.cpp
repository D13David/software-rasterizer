#if PJD_PROFILING_ENABLED

#include "profile.h"
#include "common.h"
#include "raster.h"
#include "font_render.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <float.h>

#define MAX_NODES 128
#define MAX_STACK 64
#define NAME_LENGTH 64

#define ASSERT_MAIN_THREAD() assert(IsMainThread())

struct ProfileStackEntry
{
    int     Id;
    double  StartTimeMs;
};

struct ProfileNode
{
    char    Name[NAME_LENGTH];

    // Lifetime stats
    double  Sum;
    double  Min;
    double  Max;
    int     Count;
};

struct ProfilingContext
{
    ProfileNode         Nodes[MAX_NODES];
    int                 NumNodes;
    ProfileStackEntry   Stack[MAX_STACK];
    int                 StackTop;
    LARGE_INTEGER       TimerFreq;
    DWORD               MainThreadId;
};

static ProfilingContext Context;

static PJD_INLINE double GetTimeMilliseconds()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / Context.TimerFreq.QuadPart * 1000.0;
}

static PJD_INLINE int IsMainThread()
{
    return GetCurrentThreadId() == Context.MainThreadId;
}

void ProfilerInitialize()
{
    QueryPerformanceFrequency(&Context.TimerFreq);
    Context.NumNodes = 0;
    Context.StackTop = 0;
    Context.MainThreadId = GetCurrentThreadId();
}

int ProfileRegisterNode(const char* name)
{
    assert(name != nullptr);
    assert(Context.NumNodes < MAX_NODES);

    ASSERT_MAIN_THREAD();
    int id = Context.NumNodes;

    strncpy_s(Context.Nodes[id].Name, name, NAME_LENGTH);

    Context.Nodes[id].Sum = 0.0;
    Context.Nodes[id].Min = DBL_MAX;
    Context.Nodes[id].Max = 0.0;
    Context.Nodes[id].Count = 0;

    Context.NumNodes++;
    return id;
}

void ProfileStackPushId(int id)
{
    ASSERT_MAIN_THREAD();
    assert(Context.StackTop < MAX_STACK);
    Context.Stack[Context.StackTop++] = { id, GetTimeMilliseconds() };
}

void ProfileStackPop()
{
    ASSERT_MAIN_THREAD();
    assert(Context.StackTop > 0);

    ProfileStackEntry entry = Context.Stack[--Context.StackTop];
    double duration = GetTimeMilliseconds() - entry.StartTimeMs;
    ProfileNode* node = &Context.Nodes[entry.Id];

    node->Count++;
    node->Sum += duration;
    if (duration < node->Min) node->Min = duration;
    if (duration > node->Max) node->Max = duration;
}

double ProfileGetAverage(ProfileNode* node)
{
    return node->Count ? node->Sum / node->Count : 0.0;
}

void ProfilerReset()
{
    for (int i = 0; i < Context.NumNodes; i++)
    {
        ProfileNode* node = &Context.Nodes[i];
        node->Sum = 0.0;
        node->Count = 0;
    }
    Context.StackTop = 0;
}


void DrawProfilerStats(int posX, int posY, int width)
{
    const float maxFrameBudgetMs = 1000.0f / 30.0f;
    int offsetY = posY - 16;

    for (int i = 0; i < Context.NumNodes; i++)
    {
        ProfileNode* node = &Context.Nodes[i];
        if (node->Count == 0) {
            continue;
        }

        double avg = node->Sum / node->Count;
        double total = node->Sum;

        int minX = posX + int((node->Min / maxFrameBudgetMs) * width);
        int maxX = posX + int((node->Max / maxFrameBudgetMs) * width);
        int avgX = posX + int((avg / maxFrameBudgetMs) * width);
        int totalX = posX + int((total / maxFrameBudgetMs) * width);

        offsetY += 16;

        FntWriteString(node->Name, posX, offsetY - 6);

        offsetY += 8;

        srDrawRectangle(posX, offsetY - 4, totalX - posX, 8, COLOR(0.75f, 0.75f, 0.75f));
        srDrawLine(minX, offsetY - 4, minX, offsetY + 4, COLOR(0, 1, 0));
        srDrawLine(maxX, offsetY - 4, maxX, offsetY + 4, COLOR(1, 0, 0));
        srDrawLine(avgX, offsetY - 4, avgX, offsetY + 4, COLOR(0, 0, 1));

        FntWriteString(Format("%.03fms (%.03fms)", total, avg), posX + width, offsetY - 4);
    }
}
#endif // #if PJD_PROFILING_ENABLED
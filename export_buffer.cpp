#include "export_buffer.h"
#include "common.h"

#include <assert.h>
#include <atomic>

using atomic_size = std::atomic<size_t>;
using atomic_bool = std::atomic<bool>;

typedef struct Range
{
    size_t      Offset;
    size_t      Size;
    atomic_bool Published;
} Range;

typedef struct ExportBuffer
{
    size_t      Capacity;
    alignas(64) atomic_size ReservedOffset;
    alignas(64) atomic_size NextRegionIndex;

    size_t      MaxRanges;
    Range*      Ranges;
    size_t      PublishedOffset;
    size_t      PublishedRegionIndex;

    CRITICAL_SECTION RangeLock;

    uint8_t     Memory[1];
} ExportBuffer;

ExportBufferHandle ExportBufferCreate(size_t capacity, size_t maxRegions)
{
    if (capacity == 0 || maxRegions == 0) {
        return NULL;
    }

    ExportBuffer* buffer = (ExportBuffer*)malloc(sizeof(ExportBuffer) + capacity - 1);
    if (!buffer) {
        goto failed;
    }

    buffer->Capacity = capacity;
    buffer->ReservedOffset.store(0, std::memory_order_relaxed);
    buffer->PublishedOffset = 0;
    buffer->MaxRanges = maxRegions;
    buffer->Ranges = (Range*)malloc(sizeof(Range) * maxRegions);
    if (!buffer->Ranges) {
        goto failed;
    }

    for (size_t i = 0; i < maxRegions; ++i)
    {
        buffer->Ranges[i].Published.store(true, std::memory_order_relaxed);
        buffer->Ranges[i].Offset = 0;
        buffer->Ranges[i].Size = 0;
    }
    buffer->NextRegionIndex = 0;
    buffer->PublishedRegionIndex = 0;

    InitializeCriticalSection(&buffer->RangeLock);

    return buffer;

failed:
    if (buffer) free(buffer);
    return NULL;
}

void ExportBufferDestroy(ExportBufferHandle buffer)
{
    if (buffer == NULL) {
        return;
    }
    free(buffer->Ranges);
    free(buffer);
}

static PJD_INLINE uint8_t* ExportBufferMemory(ExportBuffer* buffer)
{
    assert(buffer != NULL);
    return buffer->Memory;
}

void* ExportBufferReserve(ExportBufferHandle buffer, size_t size, size_t* outOffset, Range** outRegion)
{
    if (buffer == NULL || size == 0) {
        return NULL;
    }

    size_t currentOffset = std::atomic_load_explicit(&buffer->ReservedOffset, std::memory_order_relaxed);

    while (true)
    {
        if (currentOffset > buffer->Capacity) {
            return NULL;
        }

        if (size > buffer->Capacity - currentOffset) {
            return NULL;
        }

        const size_t newOffset = currentOffset + size;

        if (std::atomic_compare_exchange_weak_explicit(
                &buffer->ReservedOffset,
                &currentOffset, 
                newOffset, 
                std::memory_order_acq_rel, 
                std::memory_order_relaxed)) 
        {
            EnterCriticalSection(&buffer->RangeLock);
            // assign a range slot
            size_t rangeIndex = std::atomic_fetch_add_explicit(&buffer->NextRegionIndex, 1, std::memory_order_relaxed);
            assert(rangeIndex < buffer->MaxRanges);

            Range* range = &buffer->Ranges[rangeIndex % buffer->MaxRanges];
            range->Offset = currentOffset;
            range->Size = size;
            range->Published.store(false, std::memory_order_relaxed);

            if (outOffset) *outOffset = currentOffset;
            if (outRegion) *outRegion = range;
            LeaveCriticalSection(&buffer->RangeLock);

            return &buffer->Memory[currentOffset];
        }
    }
}

void ExportBufferPublish(ExportBufferHandle buffer, Range* range)
{
    assert(buffer && range);
    
    range->Published.store(true, std::memory_order_release);
}

size_t ExportBufferUsed(ExportBufferHandle buffer)
{
    if (!buffer) {
        return 0;
    }

    size_t idx = buffer->PublishedRegionIndex;
    size_t published = buffer->PublishedOffset;

    while (idx != buffer->NextRegionIndex.load(std::memory_order_acquire))
    {
        Range* range = &buffer->Ranges[idx % buffer->MaxRanges];
        if (!range->Published.load(std::memory_order_acquire)) {
            break;
        }

        published += range->Size;
        idx++;
    }
    buffer->PublishedRegionIndex = idx;
    buffer->PublishedOffset = published;

    return published;
}

void* ExportBufferData(ExportBufferHandle buffer)
{
    if (!buffer) {
        return NULL;
    }
    return ExportBufferMemory(buffer);
}

size_t ExportBufferCapacity(ExportBufferHandle buffer)
{
    if (!buffer) {
        return 0;
    }
    return buffer->Capacity;
}

void ExportBufferReset(ExportBufferHandle buffer)
{
    if (buffer == NULL) {
        return;
    }

    buffer->ReservedOffset.store(0, std::memory_order_relaxed);
    buffer->NextRegionIndex.store(0, std::memory_order_relaxed);
    buffer->PublishedOffset = 0;
    buffer->PublishedRegionIndex = 0;
}
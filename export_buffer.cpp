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
    atomic_size ReservedOffset;
    atomic_size PublishedOffset;

    size_t      MaxRanges;
    Range*      Ranges;
    atomic_size NextRegionIndex;
    atomic_size PublishedRegionIndex;

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
    buffer->PublishedOffset.store(0, std::memory_order_relaxed);
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
    buffer->NextRegionIndex.store(0, std::memory_order_relaxed);
    buffer->PublishedRegionIndex.store(0, std::memory_order_relaxed);

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

static PJD_INLINE size_t Align(size_t value, size_t alignment)
{
    assert((alignment != 0) && ((alignment & (alignment - 1)) == 0));
    return (value + alignment - 1) & ~(alignment - 1);
}

static PJD_INLINE uint8_t* ExportBufferMemory(ExportBuffer* buffer)
{
    assert(buffer != NULL);
    return buffer->Memory;
}

void* ExportBufferReserve(ExportBufferHandle buffer, size_t size, size_t alignment, size_t* outOffset, Range** outRegion)
{
    if (buffer == NULL || size == 0) {
        return NULL;
    }

    size_t currentOffset = std::atomic_load_explicit(&buffer->ReservedOffset, std::memory_order_relaxed);

    while (true)
    {
        const size_t alignedOffset = Align(currentOffset, alignment);

        if (alignedOffset > buffer->Capacity) {
            return NULL;
        }

        if (size > buffer->Capacity - alignedOffset) {
            return NULL;
        }

        const size_t newOffset = alignedOffset + size;

        if (std::atomic_compare_exchange_weak_explicit(
                &buffer->ReservedOffset,
                &currentOffset, 
                newOffset, 
                std::memory_order_acq_rel, 
                std::memory_order_relaxed)) 
        {
            // assign a range slot
            size_t rangeIndex = std::atomic_fetch_add_explicit(&buffer->NextRegionIndex, 1, std::memory_order_relaxed) % buffer->MaxRanges;
            assert(rangeIndex < buffer->MaxRanges);
            Range* range = &buffer->Ranges[rangeIndex];
            range->Offset = alignedOffset;
            range->Size = size;
            range->Published.store(false, std::memory_order_relaxed);

            if (outOffset) *outOffset = alignedOffset;
            if (outRegion) *outRegion = range;

            return &buffer->Memory[alignedOffset];
        }
    }
}

void ExportBufferPublish(ExportBufferHandle buffer, Range* range)
{
    assert(buffer && range);

    range->Published.store(true, std::memory_order_release);

    size_t idx = buffer->PublishedRegionIndex.load(std::memory_order_acquire);
    size_t published = buffer->PublishedOffset.load(std::memory_order_acquire);

    while (idx != buffer->NextRegionIndex.load(std::memory_order_acquire))
    {
        Range* r = &buffer->Ranges[idx % buffer->MaxRanges];
        if (!r->Published.load(std::memory_order_acquire)) {
            break;
        }

        published += r->Size;
        idx++;

        buffer->PublishedRegionIndex.store(idx, std::memory_order_release);
        buffer->PublishedOffset.store(published, std::memory_order_release);
    }
}

size_t ExportBufferUsed(ExportBufferHandle buffer)
{
    if (!buffer) {
        return 0;
    }
    return buffer->PublishedOffset.load(std::memory_order_acquire);
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
    buffer->PublishedOffset.store(0, std::memory_order_relaxed);
    buffer->NextRegionIndex.store(0, std::memory_order_relaxed);
    buffer->PublishedRegionIndex.store(0, std::memory_order_relaxed);
}
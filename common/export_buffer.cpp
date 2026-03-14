#include "export_buffer.h"
#include "common.h"
#include "renderer/raster.h"

#include <assert.h>
#include <atomic>

using atomic_size = std::atomic<size_t>;
using atomic_bool = std::atomic<bool>;

typedef struct RangeInt : Range
{
    atomic_bool Published;
} RangeInt;

typedef struct ExportBuffer
{
    size_t      Capacity;

    // memory head
    alignas(64) atomic_size ReservedOffset;

    // range allocator
    size_t      MaxRanges;
    alignas(64) atomic_size NextRegionIndex;
    RangeInt*   Ranges;

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
    buffer->Ranges = (RangeInt*)malloc(sizeof(RangeInt) * maxRegions);
    if (!buffer->Ranges) {
        goto failed;
    }

    for (size_t i = 0; i < maxRegions; ++i)
    {
        buffer->Ranges[i].Published.store(true, std::memory_order_relaxed);
        buffer->Ranges[i].Ptr = NULL;
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

const Range* ExportBufferReserve(ExportBufferHandle buffer, size_t size)
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

        // CAS allocate space in backing memory, loop till fitting range was found
        if (!std::atomic_compare_exchange_weak_explicit(&buffer->ReservedOffset, &currentOffset,
            newOffset, std::memory_order_acq_rel, std::memory_order_relaxed)) 
        {
            continue;
        }

        // allocate a slot for the new range
        size_t rangeIndex = std::atomic_fetch_add_explicit(&buffer->NextRegionIndex, 1, std::memory_order_relaxed);
        if (rangeIndex >= buffer->MaxRanges) 
        {
            return NULL;
        }

        RangeInt* range = &buffer->Ranges[rangeIndex];
        range->Ptr = &buffer->Memory[currentOffset];
        range->Size = size;
        range->Published.store(false, std::memory_order_relaxed);

        return range;
    }
}

void ExportBufferPublish(ExportBufferHandle buffer, const Range* range)
{
    assert(buffer && range);
    ((RangeInt*)range)->Published.store(true, std::memory_order_release);
}

Range ExportBufferReadPublished(ExportBufferHandle buffer)
{
    if (!buffer) {
        return Range{ NULL, 0 };
    }

    size_t idx       = buffer->PublishedRegionIndex;
    size_t published = buffer->PublishedOffset;

    const size_t next = buffer->NextRegionIndex.load(std::memory_order_acquire);

    while (idx != next)
    {
        RangeInt* range = &buffer->Ranges[idx];

        if (!range->Published.load(std::memory_order_acquire)) {
            break;
        }

        published += range->Size;
        ++idx;
    }

    size_t publishedRangeSize = published - buffer->PublishedOffset;

    if (publishedRangeSize == 0) {
        return Range{ NULL, 0 };
    }

    Range result = {
        .Ptr = &buffer->Memory[buffer->PublishedOffset],
        .Size = publishedRangeSize
    };

    buffer->PublishedRegionIndex = idx;
    buffer->PublishedOffset = published;

    return result;
}

size_t ExportBufferCapacity(ExportBufferHandle buffer)
{
    if (!buffer) {
        return 0;
    }
    return buffer->Capacity;
}

void ExportBufferReset(ExportBufferHandle buffer, bool forceFlush)
{
    if (buffer == NULL) {
        return;
    }

    if (!forceFlush && ((buffer->ReservedOffset / (float)buffer->Capacity) < 0.75f)) {
        return;
    }

    buffer->ReservedOffset.store(0, std::memory_order_relaxed);
    buffer->NextRegionIndex.store(0, std::memory_order_relaxed);
    buffer->PublishedOffset = 0;
    buffer->PublishedRegionIndex = 0;
}

void DebugDrawExportBufferBuckets(ExportBufferHandle buffer, int x, int y, int width, int height)
{
    if (!buffer) return;

    RasterMode2D(true);

    // --- Stats Overlay ---
    size_t reserved = buffer->ReservedOffset.load(std::memory_order_acquire);
    size_t published = buffer->PublishedOffset;
    size_t maxRanges = buffer->MaxRanges;
    size_t nextIndex = buffer->NextRegionIndex.load(std::memory_order_acquire);
    size_t pubIndex = buffer->PublishedRegionIndex;

    // Count published/reserved/unused regions
    size_t publishedRegions = 0;
    size_t reservedRegions = 0;
    for (size_t i = 0; i < nextIndex; ++i)
    {
        if (buffer->Ranges[i].Published.load(std::memory_order_acquire))
            publishedRegions++;
        else
            reservedRegions++;
    }
    size_t unusedRegions = maxRanges - nextIndex;

    int line = 0;
    WriteString(Format("Buffer Capacity: %.2f MB", buffer->Capacity / 1024.0f / 1024.0f), x, y + line * 16); line++;
    WriteString(Format("Reserved: %.2f MB (%.1f%%)", reserved / 1024.0f / 1024.0f, (float)reserved / buffer->Capacity * 100.0f), x, y + line * 16); line++;
    WriteString(Format("Published: %.2f MB (%.1f%%)", published / 1024.0f / 1024.0f, (float)published / buffer->Capacity * 100.0f), x, y + line * 16); line++;
    WriteString(Format("Regions: %zu/%zu (Published: %zu, Reserved: %zu, Unused: %zu)", nextIndex, maxRanges, publishedRegions, reservedRegions, unusedRegions), x, y + line * 16); line++;

    if (nextIndex > 0)
    {
        float avgSize = (float)reserved / nextIndex;
        WriteString(Format("Average Range Size: %.1f bytes", avgSize), x, y + line * 16); line++;
    }

    // --- Memory Usage Bar ---
    int barHeight = 16;
    int barY = y + line * 16 + 4;
    DrawRectangle(x, barY, width, barHeight, COLOR(0.1f, 0.1f, 0.1f), SOLID_FILL);

    int reservedW = (int)((float)reserved / buffer->Capacity * width);
    int publishedW = (int)((float)published / buffer->Capacity * width);

    DrawRectangle(x, barY, reservedW, barHeight, COLOR(1.0f, 1.0f, 0.0f), SOLID_FILL); 
    DrawRectangle(x, barY, publishedW, barHeight, COLOR(0.0f, 1.0f, 0.0f), SOLID_FILL);

    // --- Compressed Region Queue ---
    int regionBarY = barY + barHeight + 8;
    int regionBarH = 50;
    const size_t bucketCount = width;
    const size_t regionsPerBucket = (maxRanges + bucketCount - 1) / bucketCount;

    for (size_t b = 0; b < bucketCount; ++b)
    {
        size_t startIdx = b * regionsPerBucket;
        size_t endIdx = startIdx + regionsPerBucket;
        if (endIdx > maxRanges) endIdx = maxRanges;

        size_t pub = 0, res = 0, unused = 0;
        for (size_t i = startIdx; i < endIdx; ++i)
        {
            if (i < nextIndex)
            {
                RangeInt& r = buffer->Ranges[i];
                if (r.Published.load(std::memory_order_acquire)) pub++;
                else res++;
            }
            else unused++;
        }

        int y0 = regionBarY;
        if (pub)
        {
            int h = (int)((float)pub / regionsPerBucket * regionBarH);
            DrawLine(x + (int)b, y0, x + (int)b, y0 + h, COLOR(0.0f, 1.0f, 0.0f));
            y0 += h;
        }
        if (res)
        {
            int h = (int)((float)res / regionsPerBucket * regionBarH);
            DrawLine(x + (int)b, y0, x + (int)b, y0 + h, COLOR(1.0f, 1.0f, 0.0f));
            y0 += h;
        }
        if (unused)
        {
            int h = regionBarH - (y0 - regionBarY);
            if (h > 0) DrawLine(x + (int)b, y0, x + (int)b, y0 + h, COLOR(0.25f, 0.25f, 0.25f));
        }
    }
}
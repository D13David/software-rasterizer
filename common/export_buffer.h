#ifndef PJD_EXPORT_BUFFER_H
#define PJD_EXPORT_BUFFER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExportBuffer* ExportBufferHandle;

typedef struct Range
{
    uint8_t*    Ptr;
    size_t      Size;
} Range;

ExportBufferHandle ExportBufferCreate(size_t capacity, size_t maxRanges);
void ExportBufferDestroy(ExportBufferHandle buffer);
size_t ExportBufferCapacity(ExportBufferHandle buffer);
void ExportBufferReset(ExportBufferHandle buffer, bool forceFlush);

// producer API
const Range* ExportBufferReserve(ExportBufferHandle buffer, size_t size);
void ExportBufferPublish(ExportBufferHandle buffer, const Range* range);

// consumer API
Range ExportBufferReadPublished(ExportBufferHandle buffer);

void DebugDrawExportBufferBuckets(ExportBufferHandle buffer, int x, int y, int width, int height);

#ifdef __cplusplus
}
#endif

#endif // PJD_EXPORT_BUFFER_H
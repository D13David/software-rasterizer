#ifndef PJD_EXPORT_BUFFER_H
#define PJD_EXPORT_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExportBuffer* ExportBufferHandle;

ExportBufferHandle ExportBufferCreate(size_t capacity, size_t maxRanges);
void ExportBufferDestroy(ExportBufferHandle buffer);
void* ExportBufferReserve(ExportBufferHandle buffer, size_t size, size_t alignment, size_t* outOffset, struct Range** outRange);
void ExportBufferPublish(ExportBufferHandle buffer, Range* range);
size_t ExportBufferUsed(ExportBufferHandle buffer);
void* ExportBufferData(ExportBufferHandle buffer);
size_t ExportBufferCapacity(ExportBufferHandle buffer);
void ExportBufferReset(ExportBufferHandle buffer);

#ifdef __cplusplus
}
#endif

#endif // PJD_EXPORT_BUFFER_H
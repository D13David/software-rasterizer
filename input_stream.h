#ifndef PJD_INPUT_STREAM
#define PJD_INPUT_STREAM

#include "raster_types.h"

#include <winnt.h>

#if __cplusplus
extern "C" {
#endif

void 
InputStreamComputeOffsets (
    InputElementDescriptor* elements, 
    int numElements
    );

size_t 
InputStreamStride (
    const InputElementDescriptor* elements, 
    int numElements, 
    uint32_t streamIndex = 0
    );

const InputElementDescriptor* 
InputStreamElementByType (
    const InputElementDescriptor* elements,
    int numElements,
    InputElementType type,
    uint32_t typeIndex = 0,
    uint32_t streamIndex = UINT32_MAX
    );

const void* 
InputStreamElementPtr (
    const void* stream,
    const InputElementDescriptor* element,
    size_t stride,
    size_t index
    );

#if __cplusplus
}
#endif

#endif // PJD_INPUT_STREAM
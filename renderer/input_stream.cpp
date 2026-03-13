#include "input_stream.h"

static int FormatToSize(InputElementFormat format)
{
    switch (format)
    {
    case InputElementFormat::FLOAT2:  return sizeof(float) * 2;
    case InputElementFormat::FLOAT3:  return sizeof(float) * 3;
    case InputElementFormat::FLOAT4:  return sizeof(float) * 4;
    case InputElementFormat::UINT8_4: return sizeof(uint8_t) * 4;
    default:
        assert(false && "format not supported");
        return -1;
    }
}

void InputStreamComputeOffsets(InputElementDescriptor* elements, int numElements)
{
    assert(numElements > 0);

    for (int i = 0; i < numElements; ++i)
    {
        if (elements[i].Offset == APPEND_ALIGNED_ELEMENT)
        {
            if (i == 0 || elements[i].StreamIndex != elements[i - 1].StreamIndex)
            {
                elements[i].Offset = 0;
            }
            else
            {
                elements[i].Offset = elements[i - 1].Offset + FormatToSize(elements[i - 1].Format);
            }
        }
    }
}

size_t InputStreamStride(const InputElementDescriptor* elements, int numElements, uint32_t streamIndex /*= 0*/)
{
    assert(elements != NULL);
    assert(numElements >= 1);

    uint32_t maxOffset = 0;
    InputElementFormat lastElementFormat = InputElementFormat::FLOAT3;

    for (int i = 0; i < numElements; ++i)
    {
        if (elements[i].StreamIndex != streamIndex) {
            continue;
        }
        if (elements[i].Offset >= maxOffset)
        {
            maxOffset = elements[i].Offset;
            lastElementFormat = elements[i].Format;
        }
    }

    return maxOffset + FormatToSize(lastElementFormat);
}

const InputElementDescriptor* InputStreamElementByType(const InputElementDescriptor* elements, int numElements, InputElementType type, uint32_t typeIndex /*= 0*/, uint32_t streamIndex /*= UINT32_MAX*/)
{
    for (int i = 0; i < numElements; ++i)
    {
        if (elements[i].Type == type &&
            elements[i].TypeIndex == typeIndex &&
            (streamIndex == UINT32_MAX || elements[i].StreamIndex == streamIndex))
        {
            return &elements[i];
        }
    }
    return NULL;
}

const void* InputStreamElementPtr(const void* stream, const InputElementDescriptor* element, size_t stride, size_t index)
{
    const uint8_t* elementStart = (const uint8_t*)stream + element->Offset;
    return elementStart + index * stride;
}
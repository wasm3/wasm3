//
//
//  Created by Steven Massey on 4/15/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#define M3_IMPLEMENT_ERROR_STRINGS
#include "m3.h"

#include "m3_core.h"

void m3NotImplemented() {
    puts("Error: Not implemented");
    abort();
}

void m3AbortIfNot(bool condition) {
    if (!condition) {
        puts("Error: Fatal");
        abort();
    }
}

M3_WEAK
void m3Yield ()
{
}

#if d_m3FixedHeap

static u8 fixedHeap[d_m3FixedHeap];
static u8* fixedHeapPtr = fixedHeap;
static u8* const fixedHeapEnd = fixedHeap + d_m3FixedHeap;
static u8* fixedHeapLast = NULL;

#if d_m3FixedHeapAlign > 1
#   define HEAP_ALIGN_PTR(P) P = (u8*)(((size_t)(P)+(d_m3FixedHeapAlign-1)) & ~ (d_m3FixedHeapAlign-1));
#else
#   define HEAP_ALIGN_PTR(P)
#endif

M3Result  m3Malloc  (void ** o_ptr, size_t i_size)
{
    u8 * ptr = fixedHeapPtr;

    fixedHeapPtr += i_size;
    HEAP_ALIGN_PTR(fixedHeapPtr);

    if (fixedHeapPtr >= fixedHeapEnd)
    {
        * o_ptr = NULL;

        return c_m3Err_mallocFailed;
    }

    memset (ptr, 0x0, i_size);
    * o_ptr = ptr;
    fixedHeapLast = ptr;

    //printf("== alloc %d => %p\n", i_size, ptr);

    return c_m3Err_none;
}

void        m3Free_impl             (void * o_ptr)
{
    if (!o_ptr) return;

    // Handle the last chunk
    if (o_ptr == fixedHeapLast) {
        fixedHeapPtr = fixedHeapLast;
        fixedHeapLast = NULL;
        //printf("== free %p\n", o_ptr);
    } else {
        //printf("== free %p [failed]\n", o_ptr);
    }
}

void *  m3Realloc  (void * i_ptr, size_t i_newSize, size_t i_oldSize)
{
    //printf("== realloc %p => %d\n", i_ptr, i_newSize);

    void * ptr = i_ptr;
    if (i_newSize == i_oldSize) return ptr;

    // Handle the last chunk
    if (i_ptr && i_ptr == fixedHeapLast) {
        fixedHeapPtr = fixedHeapLast + i_newSize;
        HEAP_ALIGN_PTR(fixedHeapPtr);
        return ptr;
    }

    m3Malloc(&ptr, i_newSize);
    if (!ptr) return NULL;

    if (i_ptr) {
        memcpy(ptr, i_ptr, i_oldSize);
    }

    return ptr;
}


#else

M3Result  m3Malloc  (void ** o_ptr, size_t i_size)
{
    M3Result result = c_m3Err_none;

    void * ptr = malloc (i_size);
    if (ptr)
    {
        memset (ptr, 0x0, i_size);
    }
    else result = c_m3Err_mallocFailed;

    * o_ptr = ptr;
    //printf("== alloc %d => %p\n", i_size, ptr);

    return result;
}

void  m3Free_impl  (void * o_ptr)
{
    if (!o_ptr) return;

    //printf("== free %p\n", o_ptr);
    free(o_ptr);
}

void *  m3Realloc  (void * i_ptr, size_t i_newSize, size_t i_oldSize)
{
    //printf("== realloc %p => %d\n", i_ptr, i_newSize);
    void * ptr = i_ptr;

    if (i_newSize != i_oldSize)
    {
        ptr = realloc (i_ptr, i_newSize);

        if (ptr)
        {
            if (i_ptr)
            {
                if (i_newSize > i_oldSize)
                    memset ((u8*) ptr + i_oldSize, 0x0, i_newSize - i_oldSize);
            }
            else memset (ptr, 0x0, i_newSize);
        }
    }

    return ptr;
}

#endif

//--------------------------------------------------------------------------------------------

#if d_m3LogNativeStack

static size_t stack_start;
static size_t stack_end;

void        m3StackCheckInit ()
{
    char stack;
    stack_end = stack_start = (size_t)&stack;
}

void        m3StackCheck ()
{
    char stack;
    size_t addr = (size_t)&stack;
    stack_end = min(stack_end, addr);
}

size_t      m3StackGetMax  ()
{
    return stack_start - stack_end;
}

#endif

//--------------------------------------------------------------------------------------------

bool  IsFpType  (u8 i_m3Type)
{
    return (i_m3Type == c_m3Type_f32 or i_m3Type == c_m3Type_f64);
}


bool  IsIntType  (u8 i_m3Type)
{
    return (i_m3Type == c_m3Type_i32 or i_m3Type == c_m3Type_i64);
}


u32  SizeOfType  (u8 i_m3Type)
{
    u32 size = sizeof (i64);

    if (i_m3Type == c_m3Type_i32 or i_m3Type == c_m3Type_f32)
        size = sizeof (i32);

    return size;
}


M3Result NormalizeType (u8 * o_type, i8 i_convolutedWasmType)
{
    M3Result result = c_m3Err_none;

    u8 type = -i_convolutedWasmType;

    if (type == 0x40)
        type = c_m3Type_none;
    else if (type < c_m3Type_i32 or type > c_m3Type_f64)
        result = c_m3Err_invalidTypeId;

    * o_type = type;

    return result;
}


//-- Binary Wasm parsing utils  ------------------------------------------------------------------------------------------


M3Result  Read_u64  (u64 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;
    ptr += sizeof (u64);

    if (ptr <= i_end)
    {
        * o_value = * ((u64 *) * io_bytes);
        * io_bytes = ptr;
        return c_m3Err_none;
    }
    else return c_m3Err_wasmUnderrun;
}


M3Result  Read_u32  (u32 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;
    ptr += sizeof (u32);

    if (ptr <= i_end)
    {
        * o_value = * ((u32 *) * io_bytes);
        * io_bytes = ptr;
        return c_m3Err_none;
    }
    else return c_m3Err_wasmUnderrun;
}

M3Result  Read_f64  (f64 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;
    ptr += sizeof (f64);

    if (ptr <= i_end)
    {
        * o_value = * ((f64 *) * io_bytes);
        * io_bytes = ptr;
        return c_m3Err_none;
    }
    else return c_m3Err_wasmUnderrun;
}


M3Result  Read_f32  (f32 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;
    ptr += sizeof (f32);

    if (ptr <= i_end)
    {
        * o_value = * ((f32 *) * io_bytes);
        * io_bytes = ptr;
        return c_m3Err_none;
    }
    else return c_m3Err_wasmUnderrun;
}


M3Result  Read_u8  (u8 * o_value, bytes_t  * io_bytes, cbytes_t i_end)
{
    const u8 * ptr = * io_bytes;

    if (ptr < i_end)
    {
        * o_value = * ptr;
        ptr += sizeof (u8);
        * io_bytes = ptr;

        return c_m3Err_none;
    }
    else return c_m3Err_wasmUnderrun;
}


M3Result  ReadLebUnsigned  (u64 * o_value, u32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = c_m3Err_wasmUnderrun;

    u64 value = 0;

    u32 shift = 0;
    const u8 * ptr = * io_bytes;

    while (ptr < i_end)
    {
        u64 byte = * (ptr++);

        value |= ((byte & 0x7f) << shift);
        shift += 7;

        if ((byte & 0x80) == 0)
        {
            result = c_m3Err_none;
            break;
        }

        if (shift > i_maxNumBits)
        {
            result = c_m3Err_lebOverflow;
            break;
        }
    }

    * o_value = value;
    * io_bytes = ptr;

    return result;
}


M3Result  ReadLebSigned  (i64 * o_value, u32 i_maxNumBits, bytes_t * io_bytes, cbytes_t i_end)
{
    M3Result result = c_m3Err_wasmUnderrun;

    i64 value = 0;

    u32 shift = 0;
    const u8 * ptr = * io_bytes;

    while (ptr < i_end)
    {
        u64 byte = * (ptr++);

        value |= ((byte & 0x7f) << shift);
        shift += 7;

        if ((byte & 0x80) == 0)
        {
            result = c_m3Err_none;

            if ((byte & 0x40) and (shift < 64))    // do sign extension
            {
                u64 extend = 1;
                extend <<= shift;
                value |= -extend;
            }

            break;
        }

        if (shift > i_maxNumBits)
        {
            result = c_m3Err_lebOverflow;
            break;
        }
    }

    * o_value = value;
    * io_bytes = ptr;

    return result;
}


M3Result  ReadLEB_u32  (u32 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    u64 value;
    M3Result result = ReadLebUnsigned (& value, 32, io_bytes, i_end);
    * o_value = (u32) value;

    return result;
}


M3Result  ReadLEB_u7  (u8 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    u64 value;
    M3Result result = ReadLebUnsigned (& value, 7, io_bytes, i_end);
    * o_value = (u8) value;

    return result;
}


M3Result  ReadLEB_i7  (i8 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    i64 value;
    M3Result result = ReadLebSigned (& value, 7, io_bytes, i_end);
    * o_value = (i8) value;

    return result;
}


M3Result  ReadLEB_i32  (i32 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    i64 value;
    M3Result result = ReadLebSigned (& value, 32, io_bytes, i_end);
    * o_value = (i32) value;

    return result;
}


M3Result  ReadLEB_i64  (i64 * o_value, bytes_t * io_bytes, cbytes_t i_end)
{
    i64 value;
    M3Result result = ReadLebSigned (& value, 64, io_bytes, i_end);
    * o_value = value;

    return result;
}


M3Result  Read_utf8  (cstr_t * o_utf8, bytes_t * io_bytes, cbytes_t i_end)
{
    *o_utf8 = NULL;

    u32 utf8Length;
    M3Result result = ReadLEB_u32 (& utf8Length, io_bytes, i_end);

    if (not result)
    {
        if (utf8Length <= c_m3MaxSaneUtf8Length)
        {
            const u8 * ptr = * io_bytes;
            const u8 * end = ptr + utf8Length;

            if (end <= i_end)
            {
                char * utf8;
                result = m3Malloc ((void **) & utf8, utf8Length + 1);

                if (not result)
                {
                    memcpy (utf8, ptr, utf8Length);
                    utf8 [utf8Length] = 0;
                    * o_utf8 = utf8;
                }

                * io_bytes = end;
            }
            else result = c_m3Err_wasmUnderrun;
        }
        else result = c_m3Err_missingUTF8;
    }

    return result;
}



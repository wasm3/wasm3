//
//  m3_compile.c
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_env.h"
#include "m3_compile.h"
#include "m3_emit.h"
#include "m3_exec.h"
#include "m3_exception.h"
#include "m3_info.h"

//-------------------------------------------------------------------------------------------------------------------------

#define d_indent "     | %s"

// just want less letters and numbers to stare at down the way in the compiler table
#define i_32    c_m3Type_i32
#define i_64    c_m3Type_i64
#define f_32    c_m3Type_f32
#define f_64    c_m3Type_f64
#define none    c_m3Type_none
#define any     (u8)-1

#if d_m3HasFloat
#define FPOP(x) x
#else
#define FPOP(x) NULL
#endif

static const IM3Operation c_preserveSetSlot [] = { NULL, op_PreserveSetSlot_i32,       op_PreserveSetSlot_i64,
                                                    FPOP(op_PreserveSetSlot_f32), FPOP(op_PreserveSetSlot_f64) };
static const IM3Operation c_setSetOps [] =       { NULL, op_SetSlot_i32,               op_SetSlot_i64,
                                                    FPOP(op_SetSlot_f32),         FPOP(op_SetSlot_f64) };
static const IM3Operation c_setGlobalOps [] =    { NULL, op_SetGlobal_i32,             op_SetGlobal_i64,
                                                    FPOP(op_SetGlobal_f32),       FPOP(op_SetGlobal_f64) };
static const IM3Operation c_setRegisterOps [] =  { NULL, op_SetRegister_i32,           op_SetRegister_i64,
                                                    FPOP(op_SetRegister_f32),     FPOP(op_SetRegister_f64) };

static const IM3Operation c_ifOps [2] [2] =             { { op_i32_BranchIf_ss, op_i32_BranchIf_rs },
                                                          { op_i64_BranchIf_ss, op_i64_BranchIf_rs } };

static const IM3Operation c_intSelectOps [2] [4] =      { { op_Select_i32_rss, op_Select_i32_srs, op_Select_i32_ssr, op_Select_i32_sss },
                                                          { op_Select_i64_rss, op_Select_i64_srs, op_Select_i64_ssr, op_Select_i64_sss } };

#if d_m3HasFloat
static const IM3Operation c_fpSelectOps [2] [2] [3] = { { { op_Select_f32_sss, op_Select_f32_srs, op_Select_f32_ssr },        // selector in slot
                                                          { op_Select_f32_rss, op_Select_f32_rrs, op_Select_f32_rsr } },      // selector in reg
                                                        { { op_Select_f64_sss, op_Select_f64_srs, op_Select_f64_ssr },        // selector in slot
                                                          { op_Select_f64_rss, op_Select_f64_rrs, op_Select_f64_rsr } } };    // selector in reg
#endif

static const u16 c_m3RegisterUnallocated = 0;
static const u16 c_slotUnused = 0xffff;


M3Result  AcquireCompilationCodePage  (IM3Compilation o, IM3CodePage * o_codePage)
{
    M3Result result = m3Err_none;

    IM3CodePage page = AcquireCodePage (o->runtime);

    if (page)
    {
#       if (d_m3EnableCodePageRefCounting)
        {
            if (o->function)
            {
                IM3Function func = o->function;
                page->info.usageCount++;

                u32 index = func->numCodePageRefs++;
_               (m3ReallocArray (& func->codePageRefs, IM3CodePage, func->numCodePageRefs, index));
                func->codePageRefs [index] = page;
            }
        }
#   endif
    }
    else _throw (m3Err_mallocFailedCodePage);

    _catch:

    * o_codePage = page;

    return result;
}

void  ReleaseCompilationCodePage  (IM3Compilation o)
{
    ReleaseCodePage (o->runtime, o->page);
}

bool  IsStackPolymorphic  (IM3Compilation o)
{
    return o->block.isPolymorphic;
}

bool  IsRegisterLocation        (i16 i_location)    { return (i_location >= d_m3Reg0SlotAlias); }
bool  IsFpRegisterLocation      (i16 i_location)    { return (i_location == d_m3Fp0SlotAlias);  }
bool  IsIntRegisterLocation     (i16 i_location)    { return (i_location == d_m3Reg0SlotAlias); }

i16     GetNumBlockValues   (IM3Compilation o)      { return o->stackIndex - o->block.initStackIndex; }

u16 GetTypeNumSlots (u8 i_type)
{
#   if d_m3Use32BitSlots
        u16 n =  Is64BitType (i_type) ? 2 : 1;
        return n;
#   else
        return 1;
#   endif
}

i16  GetStackTopIndex  (IM3Compilation o)
{                                                           d_m3Assert (o->stackIndex > 0 or IsStackPolymorphic (o));
    return o->stackIndex - 1;
}


u8  GetStackTopTypeAtOffset  (IM3Compilation o, u16 i_offset)
{
    u8 type = c_m3Type_none;

    ++i_offset;
    if (o->stackIndex >= i_offset)
        type = o->typeStack [o->stackIndex - i_offset];

    return type;
}


u8  GetStackTopType  (IM3Compilation o)
{
    return GetStackTopTypeAtOffset (o, 0);
}


u8  GetStackBottomType  (IM3Compilation o, u16 i_offset)
{
    u8 type = c_m3Type_none;

    if (i_offset < o->stackIndex)
        type = o->typeStack [i_offset];

    return type;
}


bool IsStackIndexInRegister  (IM3Compilation o, u16 i_stackIndex)
{                                                                           d_m3Assert (i_stackIndex < o->stackIndex or IsStackPolymorphic (o));
    if (i_stackIndex < o->stackIndex)
        return (o->wasmStack [i_stackIndex] >= d_m3Reg0SlotAlias);
    else
        return false;
}


bool  IsStackTopIndexInRegister  (IM3Compilation o, i16 i_stackTopOffset)
{                                                                           d_m3Assert (i_stackTopOffset >= 0 or IsStackPolymorphic (o));
    if (i_stackTopOffset >= 0)
        return IsStackIndexInRegister (o, (u16) i_stackTopOffset);
    else
        return false;
}


bool  IsStackTopInRegister  (IM3Compilation o)
{
    return IsStackTopIndexInRegister (o, GetStackTopIndex (o));
}


bool  IsStackTopMinus1InRegister  (IM3Compilation o)
{
    return IsStackTopIndexInRegister (o, GetStackTopIndex (o) - 1);
}


bool  IsStackTopMinus2InRegister  (IM3Compilation o)
{
    return IsStackTopIndexInRegister (o, GetStackTopIndex (o) - 2);
}


bool  IsStackTopInSlot  (IM3Compilation o)
{
    return not IsStackTopInRegister (o);
}


u16  GetStackTopSlotIndex  (IM3Compilation o)
{
    i16 i = GetStackTopIndex (o);

    u16 slot = c_slotUnused;

    if (i >= 0)
        slot = o->wasmStack [i];

    return slot;
}


u16  GetSlotForStackIndex  (IM3Compilation o, u16 i_stackIndex)
{                                                                   d_m3Assert (i_stackIndex < o->stackIndex or IsStackPolymorphic (o));
    u16 slot = c_slotUnused;

    if (i_stackIndex < o->stackIndex)
        slot = o->wasmStack [i_stackIndex];

    return slot;
}


bool  IsValidSlot  (u16 i_slot)
{
    return (i_slot < d_m3MaxFunctionSlots);
}


bool  IsSlotAllocated  (IM3Compilation o, u16 i_slot)
{
    return o->m3Slots [i_slot];
}


void  MarkSlotAllocated  (IM3Compilation o, u16 i_slot)
{                                                                   d_m3Assert (o->m3Slots [i_slot] == 0); // shouldn't be already allocated
    o->m3Slots [i_slot] = 1;

    o->maxAllocatedSlotPlusOne = M3_MAX (o->maxAllocatedSlotPlusOne, i_slot + 1);
}


M3Result  AllocateSlotsWithinRange  (IM3Compilation o, u16 * o_slot, u8 i_type, u16 i_startSlot, u16 i_endSlot)
{
    M3Result result = m3Err_functionStackOverflow;

    u16 numSlots = GetTypeNumSlots (i_type);
    u16 searchOffset = numSlots - 1;

    if (d_m3Use32BitSlots) {
        AlignSlotIndexToType (& i_startSlot, i_type);
    }

    // search for 1 or 2 consecutive slots in the execution stack
    u16 i = i_startSlot;
    while (i + searchOffset < i_endSlot)
    {
        if (o->m3Slots [i] == 0 and o->m3Slots [i + searchOffset] == 0)
        {
            MarkSlotAllocated (o, i);

            if (numSlots == 2)
                MarkSlotAllocated (o, i + 1);

            * o_slot = i;
            result = m3Err_none;
            break;
        }

        // keep 2-slot allocations even-aligned
        i += numSlots;
    }

    return result;
}


M3Result  AllocateSlots  (IM3Compilation o, u16 * o_slot, u8 i_type)
{
    return AllocateSlotsWithinRange (o, o_slot, i_type, o->firstDynamicSlotIndex, d_m3MaxFunctionSlots);
}


M3Result  AllocateConstantSlots  (IM3Compilation o, u16 * o_slot, u8 i_type)
{
    return AllocateSlotsWithinRange (o, o_slot, i_type, o->firstConstSlotIndex, o->firstDynamicSlotIndex);
}


M3Result  IncrementSlotUsageCount  (IM3Compilation o, u16 i_slot)
{                                                                                       d_m3Assert (i_slot < d_m3MaxFunctionSlots);
    M3Result result = m3Err_none;                                                       d_m3Assert (o->m3Slots [i_slot] > 0);

    // OPTZ (memory): 'm3Slots' could still be fused with 'typeStack' if 4 bits were used to indicate: [0,1,2,many]. The many-case
    // would scan 'wasmStack' to determine the actual usage count
    if (o->m3Slots [i_slot] < 0xFF)
    {
        o->m3Slots [i_slot]++;
    }
    else result = "slot usage count overflow";

    return result;
}


void DeallocateSlot (IM3Compilation o, i16 i_slotIndex, u8 i_type)
{                                                                                       d_m3Assert (i_slotIndex >= o->firstDynamicSlotIndex);
                                                                                        d_m3Assert (o->m3Slots [i_slotIndex]);
    for (u16 i = 0; i < GetTypeNumSlots (i_type); ++i, ++i_slotIndex)
    {
        -- o->m3Slots [i_slotIndex];
    }
}


bool  IsRegisterAllocated  (IM3Compilation o, u32 i_register)
{
    return (o->regStackIndexPlusOne [i_register] != c_m3RegisterUnallocated);
}


bool  IsRegisterTypeAllocated  (IM3Compilation o, u8 i_type)
{
    return IsRegisterAllocated (o, IsFpType (i_type));
}

void  AllocateRegister  (IM3Compilation o, u32 i_register, u16 i_stackIndex)
{
    d_m3Assert (not IsRegisterAllocated (o, i_register));

    o->regStackIndexPlusOne [i_register] = i_stackIndex + 1;
}


void  DeallocateRegister  (IM3Compilation o, u32 i_register)
{
    d_m3Assert (IsRegisterAllocated (o, i_register));

    o->regStackIndexPlusOne [i_register] = c_m3RegisterUnallocated;
}


u16  GetRegisterStackIndex  (IM3Compilation o, u32 i_register)
{
    d_m3Assert (IsRegisterAllocated (o, i_register));

    return o->regStackIndexPlusOne [i_register] - 1;
}


u16  GetMaxUsedSlotPlusOne  (IM3Compilation o)
{
    while (o->maxAllocatedSlotPlusOne > o->firstDynamicSlotIndex)
    {
        if (IsSlotAllocated (o, o->maxAllocatedSlotPlusOne - 1))
            break;

        o->maxAllocatedSlotPlusOne--;
    }

    return o->maxAllocatedSlotPlusOne;
}


M3Result  PreserveRegisterIfOccupied  (IM3Compilation o, u8 i_registerType)
{
    M3Result result = m3Err_none;

    u32 regSelect = IsFpType (i_registerType);

    if (IsRegisterAllocated (o, regSelect))
    {
        u16 stackIndex = GetRegisterStackIndex (o, regSelect);
        DeallocateRegister (o, regSelect);

        u8 type = GetStackBottomType (o, stackIndex);

        // and point to a exec slot
        u16 slot = c_slotUnused;
_       (AllocateSlots (o, & slot, type));
        o->wasmStack [stackIndex] = slot;

_       (EmitOp (o, c_setSetOps [type]));
        EmitSlotOffset (o, slot);
    }

    _catch: return result;
}


// all values must be in slots before entering loop, if, and else blocks
// otherwise they'd end up preserve-copied in the block to probably different locations (if/else)
M3Result  PreserveRegisters  (IM3Compilation o)
{
    M3Result result;

_   (PreserveRegisterIfOccupied (o, c_m3Type_f64));
_   (PreserveRegisterIfOccupied (o, c_m3Type_i64));

    _catch: return result;
}


M3Result  PreserveNonTopRegisters  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    i16 stackTop = GetStackTopIndex (o);

    if (stackTop >= 0)
    {
        if (IsRegisterAllocated (o, 0))     // r0
        {
            if (GetRegisterStackIndex (o, 0) != stackTop)
_               (PreserveRegisterIfOccupied (o, c_m3Type_i64));
        }

        if (IsRegisterAllocated (o, 1))     // fp0
        {
            if (GetRegisterStackIndex (o, 1) != stackTop)
_               (PreserveRegisterIfOccupied (o, c_m3Type_f64));
        }
    }

    _catch: return result;
}


//----------------------------------------------------------------------------------------------------------------------


M3Result  Push  (IM3Compilation o, u8 i_type, u16 i_location)
{
    M3Result result = m3Err_none;

#if !d_m3HasFloat
    if (i_type == c_m3Type_f32 || i_type == c_m3Type_f64) {
        return m3Err_unknownOpcode;
    }
#endif

    u16 stackIndex = o->stackIndex++;                                       // printf ("push: %d\n", (i32) i);

    if (stackIndex < d_m3MaxFunctionStackHeight)
    {
        o->wasmStack        [stackIndex] = i_location;
        o->typeStack        [stackIndex] = i_type;

        if (IsRegisterLocation (i_location))
        {
            u32 regSelect = IsFpRegisterLocation (i_location);
            AllocateRegister (o, regSelect, stackIndex);
        }
        else
        {
            if (o->function)
            {
                // op_Entry uses this value to track and detect stack overflow
                o->function->maxStackSlots = M3_MAX (o->function->maxStackSlots, i_location + 1);
            }
        }

        if (d_m3LogWasmStack) dump_type_stack (o);
    }
    else result = m3Err_functionStackOverflow;

    return result;
}


M3Result  PushRegister  (IM3Compilation o, u8 i_type)
{
    u16 location = IsFpType (i_type) ? d_m3Fp0SlotAlias : d_m3Reg0SlotAlias;            d_m3Assert (i_type or IsStackPolymorphic (o));
    return Push (o, i_type, location);
}


M3Result  Pop  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    if (o->stackIndex > o->block.initStackIndex)
    {
        o->stackIndex--;                                                //  printf ("pop: %d\n", (i32) o->stackIndex);

        u16 slot = o->wasmStack [o->stackIndex];
        u8 type = o->typeStack [o->stackIndex];

        if (IsRegisterLocation (slot))
        {
            u32 regSelect = IsFpRegisterLocation (slot);
            DeallocateRegister (o, regSelect);
        }
        else if (slot >= o->firstDynamicSlotIndex)
        {
            DeallocateSlot (o, slot, type);
        }
    }
    else if (not IsStackPolymorphic (o))
        result = m3Err_functionStackUnderrun;

    return result;
}


M3Result  UnwindBlockStack  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    i16 initStackIndex = o->block.initStackIndex;

    u32 popCount = 0;
    while (o->stackIndex > initStackIndex )
    {
_       (Pop (o));
        ++popCount;
    }

    if (popCount)
        m3log (compile, "unwound stack top: %d", popCount);

    _catch: return result;
}


M3Result  _PushAllocatedSlotAndEmit  (IM3Compilation o, u8 i_type, bool i_doEmit)
{
    M3Result result = m3Err_none;

    u16 slot = c_slotUnused;

_   (AllocateSlots (o, & slot, i_type));
_   (Push (o, i_type, slot));

    if (i_doEmit)
        EmitSlotOffset (o, slot);

//    printf ("push: %d\n", (u32) slot);

    _catch: return result;
}


M3Result  PushAllocatedSlotAndEmit  (IM3Compilation o, u8 i_type)
{
    return _PushAllocatedSlotAndEmit (o, i_type, true);
}


M3Result  PushAllocatedSlot  (IM3Compilation o, u8 i_type)
{
    return _PushAllocatedSlotAndEmit (o, i_type, false);
}


M3Result  PushConst  (IM3Compilation o, u64 i_word, u8 i_type)
{
    M3Result result = m3Err_none;

    // Early-exit if we're not emitting
    if (!o->page) return result;

    bool matchFound = false;
    bool is64BitType = Is64BitType(i_type);

    u32 numUsedConstSlots = o->maxConstSlotIndex - o->firstConstSlotIndex;
    u16 numRequiredSlots = GetTypeNumSlots (i_type);

    // search for duplicate matching constant slot to reuse
    if (numRequiredSlots == 2 and numUsedConstSlots >= 2)
    {
        u16 firstConstSlot = o->firstConstSlotIndex;
        AlignSlotIndexToType (& firstConstSlot, c_m3Type_i64);

        for (int slot = firstConstSlot; slot < o->maxConstSlotIndex - 1; slot += 2)
        {
            if (IsSlotAllocated (o, slot) and IsSlotAllocated (o, slot + 1))
            {
                u64 constant;
                if (is64BitType) {
                    constant = * (u64 *) & o->constants [slot - o->firstConstSlotIndex];
                } else {
                    constant = * (u32 *) & o->constants [slot - o->firstConstSlotIndex];
                }

                if (constant == i_word)
                {
                    matchFound = true;
_                   (Push (o, i_type, slot));
                    break;
                }
            }
        }
    }
    else if (numRequiredSlots == 1)
    {
        for (u32 i = 0; i < numUsedConstSlots; ++i)
        {
            u16 slot = o->firstConstSlotIndex + i;

            if (IsSlotAllocated (o, slot))
            {
                u64 constant;
                if (is64BitType) {
                    constant = * (u64 *) & o->constants [i];
                } else {
                    constant = * (u32 *) & o->constants [i];
                }
                if (constant == i_word)
                {
                    matchFound = true;
_                   (Push (o, i_type, slot));
                    break;
                }
            }
        }
    }

    if (not matchFound)
    {
        u16 slot = c_slotUnused;
        result = AllocateConstantSlots (o, & slot, i_type);

        if (result) // no more constant table space; use inline constants
        {
            result = m3Err_none;

            if (Is64BitType (i_type)) {
_               (EmitOp (o, op_Const64));
                EmitWord64 (o->page, i_word);
            } else {
_               (EmitOp (o, op_Const32));
                EmitWord32 (o->page, i_word);
            }

_           (PushAllocatedSlotAndEmit (o, i_type));
        }
        else
        {
            u16 constTableIndex = slot - o->firstConstSlotIndex;

            if (is64BitType)
            {
                u64 * constant = (u64 *) & o->constants [constTableIndex];
                * constant = i_word;
            }
            else
            {
                u32 * constant = (u32 *) & o->constants [constTableIndex];
                * constant = i_word;
            }

_           (Push (o, i_type, slot));

            o->maxConstSlotIndex = M3_MAX (slot + numRequiredSlots, o->maxConstSlotIndex);
        }
    }

    _catch: return result;
}


M3Result  EmitTopSlotAndPop  (IM3Compilation o)
{
    if (IsStackTopInSlot (o))
        EmitSlotOffset (o, GetStackTopSlotIndex (o));

    return Pop (o);
}


// Or, maybe: EmitTrappingOp
M3Result  AddTrapRecord  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    if (o->function)
    {
    }

    return result;
}


M3Result  AcquirePatch  (IM3Compilation o, IM3BranchPatch * o_patch)
{
    M3Result result = m3Err_none;

    IM3BranchPatch patch = o->releasedPatches;

    if (patch)
    {
        o->releasedPatches = patch->next;
        patch->next = NULL;
    }
    else
_       (m3Alloc (& patch, M3BranchPatch, 1));

    * o_patch = patch;

    _catch: return result;
}


bool  PatchBranches  (IM3Compilation o)
{
    bool didPatch = false;

    M3CompilationScope * block = & o->block;
    pc_t pc = GetPC (o);

    IM3BranchPatch patches = block->patches;
    IM3BranchPatch endPatch = patches;

    while (patches)
    {                                                           m3log (compile, "patching location: %p to pc: %p", patches->location, pc);
        if (not patches->location)
            break;

        * (patches->location) = pc;

        endPatch = patches;
        patches = patches->next;
    }

    if (block->patches)
    {                                                           d_m3Assert (endPatch->next == NULL);
        // return patches to pool
        endPatch->next = o->releasedPatches;
        o->releasedPatches = block->patches;
        block->patches = NULL;

        didPatch = true;
    }

    return didPatch;
}

//-------------------------------------------------------------------------------------------------------------------------


M3Result CopyStackSlot (IM3Compilation o, u16 i_stackIndex, u16 i_destSlot)
{
    M3Result result = m3Err_none;

    IM3Operation op;

    u8 type = GetStackBottomType (o, i_stackIndex);
    bool inRegister = IsStackIndexInRegister (o, i_stackIndex);

    if (inRegister)
    {
        op = c_setSetOps [type];
    }
    else op = Is64BitType (type) ? op_CopySlot_64 : op_CopySlot_32;

_   (EmitOp (o, op));
    EmitSlotOffset (o, i_destSlot);

    if (not inRegister)
    {
        u16 srcSlot = GetSlotForStackIndex (o, i_stackIndex);
        EmitSlotOffset (o, srcSlot);
    }

    _catch: return result;
}


M3Result CopyTopSlot (IM3Compilation o, u16 i_destSlot)
{
    M3Result result;

    i16 stackTop = GetStackTopIndex (o);
_   (CopyStackSlot (o, (u16) stackTop, i_destSlot));

    _catch: return result;
}


// a copy-on-write strategy is used with locals. when a get local occurs, it's not copied anywhere. the stack
// entry just has a index pointer to that local memory slot.
// then, when a previously referenced local is set, the current value needs to be preserved for those references

// TODO: consider getting rid of these specialized operations: PreserveSetSlot & PreserveCopySlot.
// They likely just take up space without improving performance.
M3Result  PreservedCopyTopSlot  (IM3Compilation o, u16 i_destSlot, u16 i_preserveSlot)
{
    M3Result result = m3Err_none;             d_m3Assert (i_destSlot != i_preserveSlot);

    IM3Operation op;

    u8 type = GetStackTopType (o);

    if (IsStackTopInRegister (o))
    {
        op = c_preserveSetSlot [type];
    }
    else op = Is64BitType (type) ? op_PreserveCopySlot_64 : op_PreserveCopySlot_32;

_   (EmitOp (o, op));
    EmitSlotOffset (o, i_destSlot);

    if (IsStackTopInSlot (o))
        EmitSlotOffset (o, GetStackTopSlotIndex (o));

    EmitSlotOffset (o, i_preserveSlot);

    _catch: return result;
}



M3Result  MoveStackTopToRegister  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    if (IsStackTopInSlot (o))
    {
        u8 type = GetStackTopType (o);

_       (PreserveRegisterIfOccupied (o, type));

        IM3Operation op = c_setRegisterOps [type];

_       (EmitOp (o, op));
_       (EmitTopSlotAndPop (o));
_       (PushRegister (o, type));
    }

    _catch: return result;
}



M3Result  ReturnStackTop  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    i16 top = GetStackTopIndex (o);

    if (top >= 0)
    {
        const u16 returnSlot = 0;

        if (o->wasmStack [top] != returnSlot)
            CopyTopSlot (o, returnSlot);
    }
    else if (not IsStackPolymorphic (o))
        result = m3Err_functionStackUnderrun;

    return result;
}



// if local is unreferenced, o_preservedSlotIndex will be equal to localIndex on return
M3Result  FindReferencedLocalWithinCurrentBlock  (IM3Compilation o, u16 * o_preservedSlotIndex, u32 i_localSlot)
{
    M3Result result = m3Err_none;

    IM3CompilationScope scope = & o->block;
    i16 startIndex = scope->initStackIndex;

    while (scope->opcode == c_waOp_block)
    {
        scope = scope->outer;
        if (not scope)
            break;

        startIndex = scope->initStackIndex;
    }

    * o_preservedSlotIndex = (u16) i_localSlot;

    for (u32 i = startIndex; i < o->stackIndex; ++i)
    {
        if (o->wasmStack [i] == i_localSlot)
        {
            if (* o_preservedSlotIndex == i_localSlot)
            {
                u8 localType = GetStackBottomType (o, i_localSlot);

_               (AllocateSlots (o, o_preservedSlotIndex, localType));
            }
            else
_               (IncrementSlotUsageCount (o, * o_preservedSlotIndex));

            o->wasmStack [i] = * o_preservedSlotIndex;
        }
    }

    _catch: return result;
}



M3Result  GetBlockScope  (IM3Compilation o, IM3CompilationScope * o_scope, i32 i_depth)
{
    IM3CompilationScope scope = & o->block;

    while (i_depth--)
    {
        scope = scope->outer;
        if (not scope)
            return "invalid block depth";
    }

    * o_scope = scope;

    return m3Err_none;
}


//-------------------------------------------------------------------------------------------------------------------------


M3Result  Compile_Const_i32  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    i32 value;
_   (ReadLEB_i32 (& value, & o->wasm, o->wasmEnd));
_   (PushConst (o, value, c_m3Type_i32));                       m3log (compile, d_indent " (const i32 = %" PRIi32 ")", get_indention_string (o), value);
    _catch: return result;
}


M3Result  Compile_Const_i64  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    i64 value;
_   (ReadLEB_i64 (& value, & o->wasm, o->wasmEnd));
_   (PushConst (o, value, c_m3Type_i64));                       m3log (compile, d_indent " (const i64 = %" PRIi64 ")", get_indention_string (o), value);
    _catch: return result;
}


#if d_m3HasFloat
M3Result  Compile_Const_f32  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    union { u32 u; f32 f; } value = { 0 };

_   (Read_f32 (& value.f, & o->wasm, o->wasmEnd));              m3log (compile, d_indent " (const f32 = %" PRIf32 ")", get_indention_string (o), value.f);
_   (PushConst (o, value.u, c_m3Type_f32));

    _catch: return result;
}


M3Result  Compile_Const_f64  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    union { u64 u; f64 f; } value = { 0 };

_   (Read_f64 (& value.f, & o->wasm, o->wasmEnd));              m3log (compile, d_indent " (const f64 = %" PRIf64 ")", get_indention_string (o), value.f);
_   (PushConst (o, value.u, c_m3Type_f64));

    _catch: return result;
}
#endif

#ifdef d_m3CompileExtendedOpcode

M3Result  Compile_ExtendedOpcode  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    i32 value;

    u8 opcode;
_   (Read_u8 (& opcode, & o->wasm, o->wasmEnd));             m3log (compile, d_indent " (FC: %" PRIi32 ")", get_indention_string (o), opcode);

    i_opcode = (i_opcode << 8) | opcode;

    //printf("Extended opcode: 0x%x\n", i_opcode);

    M3Compiler compiler = GetOpInfo(i_opcode)->compiler;

    if (compiler)
        result = (* compiler) (o, i_opcode);
    else
        result = m3Err_noCompiler;

    o->previousOpcode = i_opcode;

    _catch: return result;
}

#endif

M3Result  Compile_Return  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    if (GetFunctionNumReturns (o->function))
    {
_       (ReturnStackTop (o));
_       (Pop (o));
    }

_   (EmitOp (o, op_Return));

    o->block.isPolymorphic = true;

    _catch: return result;
}


M3Result  Compile_End  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result = m3Err_none;

    // function end:
    if (o->block.depth == 0)
    {
        u8 valueType = GetSingleRetType(o->block.type);

        if (valueType)
        {
            // if there are branches to the function end, then their values are in a register
            // if the block happens to have its top in a register too, then we can patch the branch
            // to here. Otherwise, an ReturnStackTop is appended to the end of the function (at B) and
            // branches patched there.
            if (IsStackTopInRegister (o))
                PatchBranches (o);

_           (ReturnStackTop (o));
        }
        else PatchBranches (o);  // for no return type, branch to op_End

_       (EmitOp (o, op_Return));

_       (UnwindBlockStack (o));

        // B: move register to return slot for branchehs
        if (valueType)
        {
            if (PatchBranches (o))
            {
_               (PushRegister (o, valueType));
                ReturnStackTop (o);
_               (EmitOp (o, op_Return));
            }
        }
    }

    _catch: return result;
}



M3Result  Compile_SetLocal  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    u32 localIndex;
_   (ReadLEB_u32 (& localIndex, & o->wasm, o->wasmEnd));             //  printf ("--- set local: %d \n", localSlot);

    if (localIndex < GetFunctionNumArgsAndLocals (o->function))
    {
        u16 localSlot = GetSlotForStackIndex (o, localIndex);

        u16 preserveSlot;
_       (FindReferencedLocalWithinCurrentBlock (o, & preserveSlot, localSlot));  // preserve will be different than local, if referenced

        if (preserveSlot == localSlot)
_           (CopyTopSlot (o, localSlot))
        else
_           (PreservedCopyTopSlot (o, localSlot, preserveSlot))

        if (i_opcode != c_waOp_teeLocal)
_           (Pop (o));
    }
    else _throw ("local index out of bounds");

    _catch: return result;
}


M3Result  Compile_GetLocal  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

_try {

    u32 localIndex;
_   (ReadLEB_u32 (& localIndex, & o->wasm, o->wasmEnd));

    if (localIndex >= GetFunctionNumArgsAndLocals (o->function))
        _throw ("local index out of bounds");

    u8 type = GetStackBottomType (o, localIndex);
    u16 slot = GetSlotForStackIndex (o, localIndex);

_   (Push (o, type, slot));

    } _catch: return result;
}


M3Result  Compile_GetGlobal  (IM3Compilation o, M3Global * i_global)
{
    M3Result result;

    IM3Operation op = Is64BitType (i_global->type) ? op_GetGlobal_s64 : op_GetGlobal_s32;
_   (EmitOp (o, op));
    EmitPointer (o, & i_global->intValue);
_   (PushAllocatedSlotAndEmit (o, i_global->type));

    _catch: return result;
}


M3Result  Compile_SetGlobal  (IM3Compilation o, M3Global * i_global)
{
    M3Result result = m3Err_none;

    if (i_global->isMutable)
    {
        IM3Operation op;
        u8 type = GetStackTopType (o);

        if (IsStackTopInRegister (o))
        {
            op = c_setGlobalOps [type];
        }
        else op = Is64BitType (type) ? op_SetGlobal_s64 : op_SetGlobal_s32;

_      (EmitOp (o, op));
        EmitPointer (o, & i_global->intValue);

        if (IsStackTopInSlot (o))
            EmitSlotOffset (o, GetStackTopSlotIndex (o));

_      (Pop (o));
    }
    else result = m3Err_settingImmutableGlobal;

    _catch: return result;
}


M3Result  Compile_GetSetGlobal  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result = m3Err_none;

    u32 globalIndex;
_   (ReadLEB_u32 (& globalIndex, & o->wasm, o->wasmEnd));

    if (globalIndex < o->module->numGlobals)
    {
        if (o->module->globals)
        {
            M3Global * global = & o->module->globals [globalIndex];

            result = (i_opcode == 0x23) ? Compile_GetGlobal (o, global) : Compile_SetGlobal (o, global);
        }
        else result = ErrorCompile (m3Err_globalMemoryNotAllocated, o, "module '%s' is missing global memory", o->module->name);
    }
    else result = m3Err_globaIndexOutOfBounds;

    _catch: return result;
}


M3Result  Compile_Branch  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    u32 depth;
_   (ReadLEB_u32 (& depth, & o->wasm, o->wasmEnd));         //      printf ("depth: %d \n", depth);

    IM3CompilationScope scope;
_   (GetBlockScope (o, & scope, depth));

    IM3Operation op;

    // branch target is a loop (continue)
    if (scope->opcode == c_waOp_loop)
    {
        if (i_opcode == c_waOp_branchIf)
        {
_           (MoveStackTopToRegister (o));
            op = op_ContinueLoopIf;
_           (Pop (o));
        }
        else
        {
            op = op_ContinueLoop;
            o->block.isPolymorphic = true;
        }

_       (EmitOp (o, op));
        EmitPointer (o, scope->pc);
    }
    else
    {
        u16 conditionSlot = c_slotUnused;
        u16 valueSlot = c_slotUnused;
        u8 valueType = GetSingleRetType(scope->type);

        if (i_opcode == c_waOp_branchIf)
        {
            bool conditionInRegister = IsStackTopInRegister (o);

            op = conditionInRegister ? op_BranchIf_r : op_BranchIf_s;

            conditionSlot = GetStackTopSlotIndex (o);
_           (Pop (o));  // condition

            // no Pop of values here 'cause the next statement in block can consume this value
            if (IsFpType (valueType))
            {
_               (MoveStackTopToRegister (o));
            }
            else if (IsIntType (valueType))
            {
                // need to deal with int value in slot. it needs to be copied to _r0 during the branch
                if (IsStackTopInSlot (o))
                {
                    valueSlot = GetStackTopSlotIndex (o);

                    op = c_ifOps [valueType - c_m3Type_i32] [conditionInRegister];
                }
            }
        }
        else
        {
            op = op_Branch;

            if (valueType != c_m3Type_none and not IsStackPolymorphic (o))
_               (MoveStackTopToRegister (o));

            o->block.isPolymorphic = true;
        }

_       (EmitOp (o, op));
        if (IsValidSlot (conditionSlot))
            EmitSlotOffset (o, conditionSlot);
        if (IsValidSlot (valueSlot))
            EmitSlotOffset (o, valueSlot);

        IM3BranchPatch patch;
_       (AcquirePatch (o, & patch));

        patch->location = (pc_t *) ReservePointer (o);
        patch->next = scope->patches;
        scope->patches = patch;
    }

    _catch: return result;
}




M3Result  Compile_BranchTable  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

_try {
    u32 targetCount;
_   (ReadLEB_u32 (& targetCount, & o->wasm, o->wasmEnd));

_   (PreserveRegisterIfOccupied (o, c_m3Type_i64));         // move branch operand to a slot
    u16 slot = GetStackTopSlotIndex (o);
_   (Pop (o));

    // OPTZ: according to spec: "forward branches that target a control instruction with a non-empty
    // result type consume matching operands first and push them back on the operand stack after unwinding"
    // So, this move-to-reg is only necessary if the target scopes have a type.
    if (GetNumBlockValues (o) > 0)
_      (MoveStackTopToRegister (o));

    u32 numCodeLines = targetCount + 4; // 3 => IM3Operation + slot + target_count + default_target
_   (EnsureCodePageNumLines (o, numCodeLines));

_   (EmitOp (o, op_BranchTable));
    EmitSlotOffset (o, slot);
    EmitConstant32 (o, targetCount);

    IM3CodePage continueOpPage = NULL;

    ++targetCount; // include default
    for (u32 i = 0; i < targetCount; ++i)
    {
        u32 target;
_       (ReadLEB_u32 (& target, & o->wasm, o->wasmEnd));

        IM3CompilationScope scope;
_       (GetBlockScope (o, & scope, target));

        if (scope->opcode == c_waOp_loop)
        {
            // create a ContinueLoop operation on a fresh page
_           (AcquireCompilationCodePage (o, & continueOpPage));

            pc_t startPC = GetPagePC (continueOpPage);
            EmitPointer (o, startPC);

            IM3CodePage savedPage = o->page;
            o->page = continueOpPage;

_           (EmitOp (o, op_ContinueLoop));
            EmitPointer (o, scope->pc);

            ReleaseCompilationCodePage (o);     // FIX: continueOpPage can get lost if thrown
            o->page = savedPage;
        }
        else
        {
            IM3BranchPatch patch;
_           (AcquirePatch (o, & patch));

            patch->location = (pc_t *) ReservePointer (o);
            patch->next = scope->patches;
            scope->patches = patch;
        }
    }

    o->block.isPolymorphic = true;

    }

    _catch:

    return result;
}


void  AlignSlotIndexToType  (u16 * io_slotIndex, u8 i_type)
{
    // align 64-bit words to even slots
    u16 numSlots = GetTypeNumSlots (i_type);

    u16 mask = numSlots - 1;
    * io_slotIndex = (* io_slotIndex + mask) & ~mask;
}


M3Result  CompileCallArgsAndReturn  (IM3Compilation o, u16 * o_stackOffset, IM3FuncType i_type, bool i_isIndirect)
{
    M3Result result = m3Err_none;

_try {

    u16 topSlot = GetMaxUsedSlotPlusOne (o);

    // force use of at least one stack slot; this is to help ensure
    // the m3 stack overflows (and traps) before the native stack can overflow.
    // e.g. see Wasm spec test 'runaway' in call.wast
    topSlot = M3_MAX (1, topSlot);

    // stack frame is 64-bit aligned
    AlignSlotIndexToType (& topSlot, c_m3Type_i64);

    * o_stackOffset = topSlot;

    // wait to pop this here so that topSlot search is correct
    if (i_isIndirect)
_       (Pop (o));

    u32 numArgs = i_type->numArgs;

    u32 slotsPerArg = sizeof (u64) / sizeof (m3slot_t);

    // args are 64-bit aligned
    u16 argTop = topSlot + numArgs * slotsPerArg;

    while (numArgs--)
    {
_       (CopyTopSlot (o, argTop -= slotsPerArg));
_       (Pop (o));
    }

    if (i_type->numRets)
    {
        MarkSlotAllocated (o, topSlot);
_       (Push (o, GetSingleRetType(i_type), topSlot));
    }

    } _catch: return result;
}


M3Result  Compile_Call  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

_try {
    u32 functionIndex;
_   (ReadLEB_u32 (& functionIndex, & o->wasm, o->wasmEnd));

    IM3Function function = Module_GetFunction (o->module, functionIndex);

    if (function)
    {                                                                   m3log (compile, d_indent " (func= '%s'; args= %d)",
                                                                                get_indention_string (o), m3_GetFunctionName (function), function->funcType->numArgs);
        if (function->module)
        {
            // OPTZ: could avoid arg copy when args are already sequential and at top

            u16 slotTop;
_           (CompileCallArgsAndReturn (o, & slotTop, function->funcType, false));

            IM3Operation op;
            const void * operand;

            if (function->compiled)
            {
                op = op_Call;
                operand = function->compiled;
            }
            else
            {                                                           d_m3Assert (function->module);  // not linked
                op = op_Compile;
                operand = function;
            }

_           (EmitOp     (o, op));
            EmitPointer (o, operand);
            EmitSlotOffset  (o, slotTop);
        }
        else
        {
            result = ErrorCompile (m3Err_functionImportMissing, o, "'%s.%s'", GetFunctionImportModuleName (function), m3_GetFunctionName (function));
        }
    }
    else result = m3Err_functionLookupFailed;

    } _catch: return result;
}


M3Result  Compile_CallIndirect  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

_try {
    u32 typeIndex;
_   (ReadLEB_u32 (& typeIndex, & o->wasm, o->wasmEnd));

    i8 reserved;
_   (ReadLEB_i7 (& reserved, & o->wasm, o->wasmEnd));

    _throwif ("function type index out of range", typeIndex >= o->module->numFuncTypes);

    if (IsStackTopInRegister (o))
_       (PreserveRegisterIfOccupied (o, c_m3Type_i32));

    u16 tableIndexSlot = GetStackTopSlotIndex (o);

    u16 execTop;
    IM3FuncType type = o->module->funcTypes [typeIndex];
_   (CompileCallArgsAndReturn (o, & execTop, type, true));

_   (EmitOp         (o, op_CallIndirect));
    EmitSlotOffset  (o, tableIndexSlot);
    EmitPointer     (o, o->module);
    EmitPointer     (o, type);              // TODO: unify all types in M3Environment
    EmitSlotOffset  (o, execTop);

} _catch:
    return result;
}


M3Result  Compile_Memory_Current  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    i8 reserved;
_   (ReadLEB_i7 (& reserved, & o->wasm, o->wasmEnd));

_   (EmitOp     (o, op_MemCurrent));

_   (PushRegister (o, c_m3Type_i32));

    _catch: return result;
}


M3Result  Compile_Memory_Grow  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    i8 reserved;
_   (ReadLEB_i7 (& reserved, & o->wasm, o->wasmEnd));

_   (MoveStackTopToRegister (o));   // a stack flavor of Grow would get rid of this
_   (Pop (o));

_   (EmitOp     (o, op_MemGrow));

_   (PushRegister (o, c_m3Type_i32));

    _catch: return result;
}

static
M3Result  ReadBlockType  (IM3Compilation o, IM3FuncType * o_blockType)
{
    M3Result result;
    i64 type;
_   (ReadLebSigned (& type, 33, & o->wasm, o->wasmEnd));

    if (type < 0)
    {
        u8 valueType;
_       (NormalizeType (&valueType, type));                                m3log (compile, d_indent " (type: %s)", get_indention_string (o), c_waTypes [valueType]);
        *o_blockType = o->module->environment->retFuncTypes[valueType];
    }
    else
    {
        _throwif("func type out of bounds", type >= o->module->numFuncTypes);
        *o_blockType = o->module->funcTypes[type];                         m3log (compile, d_indent " (type: %s)", get_indention_string (o), SPrintFuncTypeSignature (*o_blockType));
    }
    _catch: return result;
}


// This preemptively preserves args and locals on the stack that might be written-to in the subsequent block
// (versus the COW strategy that happens in SetLocal within a block).  Initially, I thought I'd have to be clever and
// retroactively insert preservation code to avoid impacting general performance, but this compilation pattern doesn't
// really occur in compiled Wasm code, so PreserveArgsAndLocals generally does nothing. Still waiting on a real-world case!
M3Result  PreserveArgsAndLocals  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    if (o->stackIndex > o->firstDynamicStackIndex)
    {
        u32 numArgsAndLocals = GetFunctionNumArgsAndLocals (o->function);

        for (u32 i = 0; i < numArgsAndLocals; ++i)
        {
            u16 localSlot = GetSlotForStackIndex (o, i);
            u16 preservedSlotIndex;
_           (FindReferencedLocalWithinCurrentBlock (o, & preservedSlotIndex, localSlot));

            if (preservedSlotIndex != localSlot)
            {
                u8 type = GetStackBottomType (o, i);
                IM3Operation op = Is64BitType (type) ? op_CopySlot_64 : op_CopySlot_32;

                EmitOp          (o, op);
                EmitSlotOffset  (o, preservedSlotIndex);
                EmitSlotOffset  (o, localSlot);
            }
        }
    }

    _catch:
    return result;
}


M3Result  Compile_LoopOrBlock  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

_   (PreserveRegisters (o));
_   (PreserveArgsAndLocals (o));

    IM3FuncType blockType;
_   (ReadBlockType (o, & blockType));

    if (i_opcode == c_waOp_loop)
_       (EmitOp (o, op_Loop));

_   (CompileBlock (o, blockType, i_opcode));

    _catch: return result;
}


M3Result  CompileElseBlock  (IM3Compilation o, pc_t * o_startPC, IM3FuncType i_blockType)
{
    M3Result result;

_try {
    IM3CodePage elsePage;
_   (AcquireCompilationCodePage (o, & elsePage));

    * o_startPC = GetPagePC (elsePage);

    IM3CodePage savedPage = o->page;
    o->page = elsePage;

_   (CompileBlock (o, i_blockType, c_waOp_else));

_   (EmitOp (o, op_Branch));
    EmitPointer (o, GetPagePC (savedPage));

    ReleaseCompilationCodePage (o);

    o->page = savedPage;

} _catch:
    return result;
}


M3Result  Compile_If  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

_try {
_   (PreserveNonTopRegisters (o));
_   (PreserveArgsAndLocals (o));

    IM3Operation op = IsStackTopInRegister (o) ? op_If_r : op_If_s;

_   (EmitOp (o, op));
_   (EmitTopSlotAndPop (o));

    i32 stackIndex = o->stackIndex;

    pc_t * pc = (pc_t *) ReservePointer (o);

    IM3FuncType blockType;
_   (ReadBlockType (o, & blockType));

_   (CompileBlock (o, blockType, i_opcode));

    if (o->previousOpcode == c_waOp_else)
    {
        if (blockType and o->stackIndex > stackIndex)
        {
_           (Pop (o));
        }

_       (CompileElseBlock (o, pc, blockType));
    }
    else * pc = GetPC (o);

    } _catch: return result;
}


M3Result  Compile_Select  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result = m3Err_none;

    u16 slots [3] = { c_slotUnused, c_slotUnused, c_slotUnused };

    u8 type = GetStackTopTypeAtOffset (o, 1); // get type of selection

    IM3Operation op = NULL;

    if (IsFpType (type))
    {
#if d_m3HasFloat
        // not consuming a fp reg, so preserve
        if (not IsStackTopMinus1InRegister (o) and
            not IsStackTopMinus2InRegister (o))
        {
_           (PreserveRegisterIfOccupied (o, type));
        }

        bool selectorInReg = IsStackTopInRegister (o);
        slots [0] = GetStackTopSlotIndex (o);
_       (Pop (o));

        u32 opIndex = 0;

        for (u32 i = 1; i <= 2; ++i)
        {
            if (IsStackTopInRegister (o))
                opIndex = i;
            else
                slots [i] = GetStackTopSlotIndex (o);

_          (Pop (o));
        }

        op = c_fpSelectOps [type - c_m3Type_f32] [selectorInReg] [opIndex];
#else
        _throw (m3Err_unknownOpcode);
#endif
    }
    else if (IsIntType (type))
    {
        // 'sss' operation doesn't consume a register, so might have to protected its contents
        if (not IsStackTopInRegister (o) and
            not IsStackTopMinus1InRegister (o) and
            not IsStackTopMinus2InRegister (o))
        {
_           (PreserveRegisterIfOccupied (o, type));
        }

        u32 opIndex = 3;  // op_Select_*_sss

        for (u32 i = 0; i < 3; ++i)
        {
            if (IsStackTopInRegister (o))
                opIndex = i;
            else
                slots [i] = GetStackTopSlotIndex (o);

_          (Pop (o));
        }

        op = c_intSelectOps [type - c_m3Type_i32] [opIndex];
    }
    else if (not IsStackPolymorphic (o))
        _throw (m3Err_functionStackUnderrun);

    EmitOp (o, op);
    for (u32 i = 0; i < 3; i++)
    {
        if (IsValidSlot (slots [i]))
            EmitSlotOffset (o, slots [i]);
    }
_   (PushRegister (o, type));

    _catch: return result;
}


M3Result  Compile_Drop  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result = Pop (o);                                              if (d_m3LogWasmStack) dump_type_stack (o);
    return result;
}


M3Result  Compile_Nop  (IM3Compilation o, m3opcode_t i_opcode)
{
    return m3Err_none;
}


M3Result  Compile_Unreachable  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

_   (AddTrapRecord (o));

_   (EmitOp (o, op_Unreachable));
    o->block.isPolymorphic = true;

    _catch:
    return result;
}


// TODO OPTZ: currently all stack slot indices take up a full word, but
// dual stack source operands could be packed together
M3Result  Compile_Operator  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

    const M3OpInfo * op = GetOpInfo(i_opcode);

    IM3Operation operation;

    // This preserve is for for FP compare operations.
    // either need additional slot destination operations or the
    // easy fix, move _r0 out of the way.
    // moving out the way might be the optimal solution most often?
    // otherwise, the _r0 reg can get buried down in the stack
    // and be idle & wasted for a moment.
    if (IsFpType (GetStackTopType (o)) and IsIntType (op->type))
    {
_       (PreserveRegisterIfOccupied (o, op->type));
    }

    if (op->stackOffset == 0)
    {
        if (IsStackTopInRegister (o))
        {
            operation = op->operations [0]; // _s
        }
        else
        {
_           (PreserveRegisterIfOccupied (o, op->type));
            operation = op->operations [1]; // _r
        }
    }
    else
    {
        if (IsStackTopInRegister (o))
        {
            operation = op->operations [0];  // _rs

            if (IsStackTopMinus1InRegister (o))
            {                                       d_m3Assert (i_opcode == 0x38 or i_opcode == 0x39);
                operation = op->operations [3]; // _rr for fp.store
            }
        }
        else if (IsStackTopMinus1InRegister (o))
        {
            operation = op->operations [1]; // _sr

            if (not operation)  // must be commutative, then
                operation = op->operations [0];
        }
        else
        {
_           (PreserveRegisterIfOccupied (o, op->type));     // _ss
            operation = op->operations [2];
        }
    }

    if (operation)
    {
_       (EmitOp (o, operation));

_       (EmitTopSlotAndPop (o));

        if (op->stackOffset < 0)
_           (EmitTopSlotAndPop (o));

        if (op->type != c_m3Type_none)
_           (PushRegister (o, op->type));
    }
    else
    {
#       ifdef DEBUG
            result = ErrorCompile ("no operation found for opcode", o, "'%s'", op->name);
#       else
            result = ErrorCompile ("no operation found for opcode", o, "");
#       endif
    }

    _catch: return result;
}


M3Result  Compile_Convert  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result = m3Err_none;

    const M3OpInfo * opInfo = GetOpInfo(i_opcode);

    bool destInSlot = IsRegisterTypeAllocated (o, opInfo->type);
    bool sourceInSlot = IsStackTopInSlot (o);

    IM3Operation op = opInfo->operations [destInSlot * 2 + sourceInSlot];

_   (EmitOp (o, op));
_   (EmitTopSlotAndPop (o));

    if (destInSlot)
_       (PushAllocatedSlotAndEmit (o, opInfo->type))
    else
_       (PushRegister (o, opInfo->type))

    _catch: return result;
}


M3Result  Compile_Load_Store  (IM3Compilation o, m3opcode_t i_opcode)
{
    M3Result result;

_try {
    u32 alignHint, memoryOffset;

_   (ReadLEB_u32 (& alignHint, & o->wasm, o->wasmEnd));
_   (ReadLEB_u32 (& memoryOffset, & o->wasm, o->wasmEnd));
                                                                        m3log (compile, d_indent " (offset = %d)", get_indention_string (o), memoryOffset);
    const M3OpInfo * op = GetOpInfo(i_opcode);

    if (IsFpType (op->type))
_       (PreserveRegisterIfOccupied (o, c_m3Type_f64));

_   (Compile_Operator (o, i_opcode));

    EmitConstant32 (o, memoryOffset);
}
    _catch: return result;
}


// d_logOp, d_logOp2 macros aren't actually used by the compiler, just codepage decoding (d_m3LogCodePages = 1)
#define d_logOp(OP)                         { op_##OP,                  NULL,                       NULL,                       NULL }
#define d_logOp2(OP1,OP2)                   { op_##OP1,                 op_##OP2,                   NULL,                       NULL }

#define d_emptyOpList                       { NULL,                     NULL,                       NULL,                       NULL }
#define d_unaryOpList(TYPE, NAME)           { op_##TYPE##_##NAME##_r,   op_##TYPE##_##NAME##_s,     NULL,                       NULL }
#define d_binOpList(TYPE, NAME)             { op_##TYPE##_##NAME##_rs,  op_##TYPE##_##NAME##_sr,    op_##TYPE##_##NAME##_ss,    NULL }
#define d_storeFpOpList(TYPE, NAME)         { op_##TYPE##_##NAME##_rs,  op_##TYPE##_##NAME##_sr,    op_##TYPE##_##NAME##_ss,    op_##TYPE##_##NAME##_rr }
#define d_commutativeBinOpList(TYPE, NAME)  { op_##TYPE##_##NAME##_rs,  NULL,                       op_##TYPE##_##NAME##_ss,    NULL }
#define d_convertOpList(OP)                 { op_##OP##_r_r,            op_##OP##_r_s,              op_##OP##_s_r,              op_##OP##_s_s }


const M3OpInfo c_operations [] =
{
    M3OP( "unreachable",         0, none,   d_logOp (Unreachable),              Compile_Unreachable ),  // 0x00
    M3OP( "nop",                 0, none,   d_emptyOpList,                      Compile_Nop ),          // 0x01 .
    M3OP( "block",               0, none,   d_emptyOpList,                      Compile_LoopOrBlock ),  // 0x02
    M3OP( "loop",                0, none,   d_logOp (Loop),                     Compile_LoopOrBlock ),  // 0x03
    M3OP( "if",                 -1, none,   d_emptyOpList,                      Compile_If ),           // 0x04
    M3OP( "else",                0, none,   d_emptyOpList,                      Compile_Nop ),          // 0x05

    M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED,                          // 0x06 - 0x0a

    M3OP( "end",                 0, none,   d_emptyOpList,                      Compile_End ),          // 0x0b
    M3OP( "br",                  0, none,   d_logOp (Branch),                   Compile_Branch ),       // 0x0c
    M3OP( "br_if",              -1, none,   d_logOp2 (BranchIf_r, BranchIf_s),  Compile_Branch ),       // 0x0d
    M3OP( "br_table",           -1, none,   d_logOp (BranchTable),              Compile_BranchTable ),  // 0x0e
    M3OP( "return",              0, any,    d_logOp (Return),                   Compile_Return ),       // 0x0f
    M3OP( "call",                0, any,    d_logOp (Call),                     Compile_Call ),         // 0x10
    M3OP( "call_indirect",       0, any,    d_logOp (CallIndirect),             Compile_CallIndirect ), // 0x11
    M3OP( "return_call",         0, any,    d_emptyOpList,                      Compile_Call ),         // 0x12 TODO: Optimize
    M3OP( "return_call_indirect",0, any,    d_emptyOpList,                      Compile_CallIndirect ), // 0x13

    M3OP_RESERVED,  M3OP_RESERVED,                                                                      // 0x14 - 0x15
    M3OP_RESERVED,  M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED,                                        // 0x16 - 0x19

    M3OP( "drop",               -1, none,   d_emptyOpList,                      Compile_Drop ),         // 0x1a
    M3OP( "select",             -2, any,    d_emptyOpList,                      Compile_Select  ),      // 0x1b

    M3OP_RESERVED,  M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED,                                        // 0x1c - 0x1f

    M3OP( "local.get",          1,  any,    d_emptyOpList,                      Compile_GetLocal ),     // 0x20
    M3OP( "local.set",          1,  none,   d_emptyOpList,                      Compile_SetLocal ),     // 0x21
    M3OP( "local.tee",          0,  any,    d_emptyOpList,                      Compile_SetLocal ),     // 0x22
    M3OP( "global.get",         1,  none,   d_emptyOpList,                      Compile_GetSetGlobal ), // 0x23
    M3OP( "global.set",         1,  none,   d_emptyOpList,                      Compile_GetSetGlobal ), // 0x24

    M3OP_RESERVED,  M3OP_RESERVED, M3OP_RESERVED,                                                       // 0x25 - 0x27

    M3OP( "i32.load",           0,  i_32,   d_unaryOpList (i32, Load_i32),      Compile_Load_Store ),   // 0x28
    M3OP( "i64.load",           0,  i_64,   d_unaryOpList (i64, Load_i64),      Compile_Load_Store ),   // 0x29
    M3OP_F( "f32.load",         0,  f_32,   d_unaryOpList (f32, Load_f32),      Compile_Load_Store ),   // 0x2a
    M3OP_F( "f64.load",         0,  f_64,   d_unaryOpList (f64, Load_f64),      Compile_Load_Store ),   // 0x2b

    M3OP( "i32.load8_s",        0,  i_32,   d_unaryOpList (i32, Load_i8),       Compile_Load_Store ),   // 0x2c
    M3OP( "i32.load8_u",        0,  i_32,   d_unaryOpList (i32, Load_u8),       Compile_Load_Store ),   // 0x2d
    M3OP( "i32.load16_s",       0,  i_32,   d_unaryOpList (i32, Load_i16),      Compile_Load_Store ),   // 0x2e
    M3OP( "i32.load16_u",       0,  i_32,   d_unaryOpList (i32, Load_u16),      Compile_Load_Store ),   // 0x2f

    M3OP( "i64.load8_s",        0,  i_64,   d_unaryOpList (i64, Load_i8),       Compile_Load_Store ),   // 0x30
    M3OP( "i64.load8_u",        0,  i_64,   d_unaryOpList (i64, Load_u8),       Compile_Load_Store ),   // 0x31
    M3OP( "i64.load16_s",       0,  i_64,   d_unaryOpList (i64, Load_i16),      Compile_Load_Store ),   // 0x32
    M3OP( "i64.load16_u",       0,  i_64,   d_unaryOpList (i64, Load_u16),      Compile_Load_Store ),   // 0x33
    M3OP( "i64.load32_s",       0,  i_64,   d_unaryOpList (i64, Load_i32),      Compile_Load_Store ),   // 0x34
    M3OP( "i64.load32_u",       0,  i_64,   d_unaryOpList (i64, Load_u32),      Compile_Load_Store ),   // 0x35

    M3OP( "i32.store",          -2, none,   d_binOpList (i32, Store_i32),       Compile_Load_Store ),   // 0x36
    M3OP( "i64.store",          -2, none,   d_binOpList (i64, Store_i64),       Compile_Load_Store ),   // 0x37
    M3OP_F( "f32.store",        -2, none,   d_storeFpOpList (f32, Store_f32),   Compile_Load_Store ),   // 0x38
    M3OP_F( "f64.store",        -2, none,   d_storeFpOpList (f64, Store_f64),   Compile_Load_Store ),   // 0x39

    M3OP( "i32.store8",         -2, none,   d_binOpList (i32, Store_u8),        Compile_Load_Store ),   // 0x3a
    M3OP( "i32.store16",        -2, none,   d_binOpList (i32, Store_i16),       Compile_Load_Store ),   // 0x3b

    M3OP( "i64.store8",         -2, none,   d_binOpList (i64, Store_u8),        Compile_Load_Store ),   // 0x3c
    M3OP( "i64.store16",        -2, none,   d_binOpList (i64, Store_i16),       Compile_Load_Store ),   // 0x3d
    M3OP( "i64.store32",        -2, none,   d_binOpList (i64, Store_i32),       Compile_Load_Store ),   // 0x3e

    M3OP( "memory.current",     1,  i_32,   d_logOp (MemCurrent),               Compile_Memory_Current ),   // 0x3f
    M3OP( "memory.grow",        1,  i_32,   d_logOp (MemGrow),                  Compile_Memory_Grow ),      // 0x40

    M3OP( "i32.const",          1,  i_32,   d_logOp (Const32),                  Compile_Const_i32 ),    // 0x41
    M3OP( "i64.const",          1,  i_64,   d_logOp (Const64),                  Compile_Const_i64 ),    // 0x42
    M3OP_F( "f32.const",        1,  f_32,   d_emptyOpList,                      Compile_Const_f32 ),    // 0x43
    M3OP_F( "f64.const",        1,  f_64,   d_emptyOpList,                      Compile_Const_f64 ),    // 0x44

    M3OP( "i32.eqz",            0,  i_32,   d_unaryOpList (i32, EqualToZero)        , NULL  ),          // 0x45
    M3OP( "i32.eq",             -1, i_32,   d_commutativeBinOpList (i32, Equal)     , NULL  ),          // 0x46
    M3OP( "i32.ne",             -1, i_32,   d_commutativeBinOpList (i32, NotEqual)  , NULL  ),          // 0x47
    M3OP( "i32.lt_s",           -1, i_32,   d_binOpList (i32, LessThan)             , NULL  ),          // 0x48
    M3OP( "i32.lt_u",           -1, i_32,   d_binOpList (u32, LessThan)             , NULL  ),          // 0x49
    M3OP( "i32.gt_s",           -1, i_32,   d_binOpList (i32, GreaterThan)          , NULL  ),          // 0x4a
    M3OP( "i32.gt_u",           -1, i_32,   d_binOpList (u32, GreaterThan)          , NULL  ),          // 0x4b
    M3OP( "i32.le_s",           -1, i_32,   d_binOpList (i32, LessThanOrEqual)      , NULL  ),          // 0x4c
    M3OP( "i32.le_u",           -1, i_32,   d_binOpList (u32, LessThanOrEqual)      , NULL  ),          // 0x4d
    M3OP( "i32.ge_s",           -1, i_32,   d_binOpList (i32, GreaterThanOrEqual)   , NULL  ),          // 0x4e
    M3OP( "i32.ge_u",           -1, i_32,   d_binOpList (u32, GreaterThanOrEqual)   , NULL  ),          // 0x4f

    M3OP( "i64.eqz",            0,  i_32,   d_unaryOpList (i64, EqualToZero)        , NULL  ),          // 0x50
    M3OP( "i64.eq",             -1, i_32,   d_commutativeBinOpList (i64, Equal)     , NULL  ),          // 0x51
    M3OP( "i64.ne",             -1, i_32,   d_commutativeBinOpList (i64, NotEqual)  , NULL  ),          // 0x52
    M3OP( "i64.lt_s",           -1, i_32,   d_binOpList (i64, LessThan)             , NULL  ),          // 0x53
    M3OP( "i64.lt_u",           -1, i_32,   d_binOpList (u64, LessThan)             , NULL  ),          // 0x54
    M3OP( "i64.gt_s",           -1, i_32,   d_binOpList (i64, GreaterThan)          , NULL  ),          // 0x55
    M3OP( "i64.gt_u",           -1, i_32,   d_binOpList (u64, GreaterThan)          , NULL  ),          // 0x56
    M3OP( "i64.le_s",           -1, i_32,   d_binOpList (i64, LessThanOrEqual)      , NULL  ),          // 0x57
    M3OP( "i64.le_u",           -1, i_32,   d_binOpList (u64, LessThanOrEqual)      , NULL  ),          // 0x58
    M3OP( "i64.ge_s",           -1, i_32,   d_binOpList (i64, GreaterThanOrEqual)   , NULL  ),          // 0x59
    M3OP( "i64.ge_u",           -1, i_32,   d_binOpList (u64, GreaterThanOrEqual)   , NULL  ),          // 0x5a

    M3OP_F( "f32.eq",           -1, i_32,   d_commutativeBinOpList (f32, Equal)     , NULL  ),          // 0x5b
    M3OP_F( "f32.ne",           -1, i_32,   d_commutativeBinOpList (f32, NotEqual)  , NULL  ),          // 0x5c
    M3OP_F( "f32.lt",           -1, i_32,   d_binOpList (f32, LessThan)             , NULL  ),          // 0x5d
    M3OP_F( "f32.gt",           -1, i_32,   d_binOpList (f32, GreaterThan)          , NULL  ),          // 0x5e
    M3OP_F( "f32.le",           -1, i_32,   d_binOpList (f32, LessThanOrEqual)      , NULL  ),          // 0x5f
    M3OP_F( "f32.ge",           -1, i_32,   d_binOpList (f32, GreaterThanOrEqual)   , NULL  ),          // 0x60

    M3OP_F( "f64.eq",           -1, i_32,   d_commutativeBinOpList (f64, Equal)     , NULL  ),          // 0x61
    M3OP_F( "f64.ne",           -1, i_32,   d_commutativeBinOpList (f64, NotEqual)  , NULL  ),          // 0x62
    M3OP_F( "f64.lt",           -1, i_32,   d_binOpList (f64, LessThan)             , NULL  ),          // 0x63
    M3OP_F( "f64.gt",           -1, i_32,   d_binOpList (f64, GreaterThan)          , NULL  ),          // 0x64
    M3OP_F( "f64.le",           -1, i_32,   d_binOpList (f64, LessThanOrEqual)      , NULL  ),          // 0x65
    M3OP_F( "f64.ge",           -1, i_32,   d_binOpList (f64, GreaterThanOrEqual)   , NULL  ),          // 0x66

    M3OP( "i32.clz",            0,  i_32,   d_unaryOpList (u32, Clz)                , NULL  ),          // 0x67
    M3OP( "i32.ctz",            0,  i_32,   d_unaryOpList (u32, Ctz)                , NULL  ),          // 0x68
    M3OP( "i32.popcnt",         0,  i_32,   d_unaryOpList (u32, Popcnt)             , NULL  ),          // 0x69

    M3OP( "i32.add",            -1, i_32,   d_commutativeBinOpList (i32, Add)       , NULL  ),          // 0x6a
    M3OP( "i32.sub",            -1, i_32,   d_binOpList (i32, Subtract)             , NULL  ),          // 0x6b
    M3OP( "i32.mul",            -1, i_32,   d_commutativeBinOpList (i32, Multiply)  , NULL  ),          // 0x6c
    M3OP( "i32.div_s",          -1, i_32,   d_binOpList (i32, Divide)               , NULL  ),          // 0x6d
    M3OP( "i32.div_u",          -1, i_32,   d_binOpList (u32, Divide)               , NULL  ),          // 0x6e
    M3OP( "i32.rem_s",          -1, i_32,   d_binOpList (i32, Remainder)            , NULL  ),          // 0x6f
    M3OP( "i32.rem_u",          -1, i_32,   d_binOpList (u32, Remainder)            , NULL  ),          // 0x70
    M3OP( "i32.and",            -1, i_32,   d_commutativeBinOpList (u32, And)       , NULL  ),          // 0x71
    M3OP( "i32.or",             -1, i_32,   d_commutativeBinOpList (u32, Or)        , NULL  ),          // 0x72
    M3OP( "i32.xor",            -1, i_32,   d_commutativeBinOpList (u32, Xor)       , NULL  ),          // 0x73
    M3OP( "i32.shl",            -1, i_32,   d_binOpList (u32, ShiftLeft)            , NULL  ),          // 0x74
    M3OP( "i32.shr_s",          -1, i_32,   d_binOpList (i32, ShiftRight)           , NULL  ),          // 0x75
    M3OP( "i32.shr_u",          -1, i_32,   d_binOpList (u32, ShiftRight)           , NULL  ),          // 0x76
    M3OP( "i32.rotl",           -1, i_32,   d_binOpList (u32, Rotl)                 , NULL  ),          // 0x77
    M3OP( "i32.rotr",           -1, i_32,   d_binOpList (u32, Rotr)                 , NULL  ),          // 0x78

    M3OP( "i64.clz",            0,  i_64,   d_unaryOpList (u64, Clz)                , NULL  ),          // 0x79
    M3OP( "i64.ctz",            0,  i_64,   d_unaryOpList (u64, Ctz)                , NULL  ),          // 0x7a
    M3OP( "i64.popcnt",         0,  i_64,   d_unaryOpList (u64, Popcnt)             , NULL  ),          // 0x7b

    M3OP( "i64.add",            -1, i_64,   d_commutativeBinOpList (i64, Add)       , NULL  ),          // 0x7c
    M3OP( "i64.sub",            -1, i_64,   d_binOpList (i64, Subtract)             , NULL  ),          // 0x7d
    M3OP( "i64.mul",            -1, i_64,   d_commutativeBinOpList (i64, Multiply)  , NULL  ),          // 0x7e
    M3OP( "i64.div_s",          -1, i_64,   d_binOpList (i64, Divide)               , NULL  ),          // 0x7f
    M3OP( "i64.div_u",          -1, i_64,   d_binOpList (u64, Divide)               , NULL  ),          // 0x80
    M3OP( "i64.rem_s",          -1, i_64,   d_binOpList (i64, Remainder)            , NULL  ),          // 0x81
    M3OP( "i64.rem_u",          -1, i_64,   d_binOpList (u64, Remainder)            , NULL  ),          // 0x82
    M3OP( "i64.and",            -1, i_64,   d_commutativeBinOpList (u64, And)       , NULL  ),          // 0x83
    M3OP( "i64.or",             -1, i_64,   d_commutativeBinOpList (u64, Or)        , NULL  ),          // 0x84
    M3OP( "i64.xor",            -1, i_64,   d_commutativeBinOpList (u64, Xor)       , NULL  ),          // 0x85
    M3OP( "i64.shl",            -1, i_64,   d_binOpList (u64, ShiftLeft)            , NULL  ),          // 0x86
    M3OP( "i64.shr_s",          -1, i_64,   d_binOpList (i64, ShiftRight)           , NULL  ),          // 0x87
    M3OP( "i64.shr_u",          -1, i_64,   d_binOpList (u64, ShiftRight)           , NULL  ),          // 0x88
    M3OP( "i64.rotl",           -1, i_64,   d_binOpList (u64, Rotl)                 , NULL  ),          // 0x89
    M3OP( "i64.rotr",           -1, i_64,   d_binOpList (u64, Rotr)                 , NULL  ),          // 0x8a

    M3OP_F( "f32.abs",          0,  f_32,   d_unaryOpList(f32, Abs)                 , NULL  ),          // 0x8b
    M3OP_F( "f32.neg",          0,  f_32,   d_unaryOpList(f32, Negate)              , NULL  ),          // 0x8c
    M3OP_F( "f32.ceil",         0,  f_32,   d_unaryOpList(f32, Ceil)                , NULL  ),          // 0x8d
    M3OP_F( "f32.floor",        0,  f_32,   d_unaryOpList(f32, Floor)               , NULL  ),          // 0x8e
    M3OP_F( "f32.trunc",        0,  f_32,   d_unaryOpList(f32, Trunc)               , NULL  ),          // 0x8f
    M3OP_F( "f32.nearest",      0,  f_32,   d_unaryOpList(f32, Nearest)             , NULL  ),          // 0x90
    M3OP_F( "f32.sqrt",         0,  f_32,   d_unaryOpList(f32, Sqrt)                , NULL  ),          // 0x91

    M3OP_F( "f32.add",          -1, f_32,   d_commutativeBinOpList (f32, Add)       , NULL  ),          // 0x92
    M3OP_F( "f32.sub",          -1, f_32,   d_binOpList (f32, Subtract)             , NULL  ),          // 0x93
    M3OP_F( "f32.mul",          -1, f_32,   d_commutativeBinOpList (f32, Multiply)  , NULL  ),          // 0x94
    M3OP_F( "f32.div",          -1, f_32,   d_binOpList (f32, Divide)               , NULL  ),          // 0x95
    M3OP_F( "f32.min",          -1, f_32,   d_commutativeBinOpList (f32, Min)       , NULL  ),          // 0x96
    M3OP_F( "f32.max",          -1, f_32,   d_commutativeBinOpList (f32, Max)       , NULL  ),          // 0x97
    M3OP_F( "f32.copysign",     -1, f_32,   d_binOpList (f32, CopySign)             , NULL  ),          // 0x98

    M3OP_F( "f64.abs",          0,  f_64,   d_unaryOpList(f64, Abs)                 , NULL  ),          // 0x99
    M3OP_F( "f64.neg",          0,  f_64,   d_unaryOpList(f64, Negate)              , NULL  ),          // 0x9a
    M3OP_F( "f64.ceil",         0,  f_64,   d_unaryOpList(f64, Ceil)                , NULL  ),          // 0x9b
    M3OP_F( "f64.floor",        0,  f_64,   d_unaryOpList(f64, Floor)               , NULL  ),          // 0x9c
    M3OP_F( "f64.trunc",        0,  f_64,   d_unaryOpList(f64, Trunc)               , NULL  ),          // 0x9d
    M3OP_F( "f64.nearest",      0,  f_64,   d_unaryOpList(f64, Nearest)             , NULL  ),          // 0x9e
    M3OP_F( "f64.sqrt",         0,  f_64,   d_unaryOpList(f64, Sqrt)                , NULL  ),          // 0x9f

    M3OP_F( "f64.add",          -1, f_64,   d_commutativeBinOpList (f64, Add)       , NULL  ),          // 0xa0
    M3OP_F( "f64.sub",          -1, f_64,   d_binOpList (f64, Subtract)             , NULL  ),          // 0xa1
    M3OP_F( "f64.mul",          -1, f_64,   d_commutativeBinOpList (f64, Multiply)  , NULL  ),          // 0xa2
    M3OP_F( "f64.div",          -1, f_64,   d_binOpList (f64, Divide)               , NULL  ),          // 0xa3
    M3OP_F( "f64.min",          -1, f_64,   d_commutativeBinOpList (f64, Min)       , NULL  ),          // 0xa4
    M3OP_F( "f64.max",          -1, f_64,   d_commutativeBinOpList (f64, Max)       , NULL  ),          // 0xa5
    M3OP_F( "f64.copysign",     -1, f_64,   d_binOpList (f64, CopySign)             , NULL  ),          // 0xa6

    M3OP( "i32.wrap/i64",       0,  i_32,   d_unaryOpList (i32, Wrap_i64),          NULL    ),          // 0xa7
    M3OP_F( "i32.trunc_s/f32",  0,  i_32,   d_convertOpList (i32_Trunc_f32),        Compile_Convert ),  // 0xa8
    M3OP_F( "i32.trunc_u/f32",  0,  i_32,   d_convertOpList (u32_Trunc_f32),        Compile_Convert ),  // 0xa9
    M3OP_F( "i32.trunc_s/f64",  0,  i_32,   d_convertOpList (i32_Trunc_f64),        Compile_Convert ),  // 0xaa
    M3OP_F( "i32.trunc_u/f64",  0,  i_32,   d_convertOpList (u32_Trunc_f64),        Compile_Convert ),  // 0xab

    M3OP( "i64.extend_s/i32",   0,  i_64,   d_unaryOpList (i64, Extend_i32),        NULL    ),          // 0xac
    M3OP( "i64.extend_u/i32",   0,  i_64,   d_unaryOpList (i64, Extend_u32),        NULL    ),          // 0xad

    M3OP_F( "i64.trunc_s/f32",  0,  i_64,   d_convertOpList (i64_Trunc_f32),        Compile_Convert ),  // 0xae
    M3OP_F( "i64.trunc_u/f32",  0,  i_64,   d_convertOpList (u64_Trunc_f32),        Compile_Convert ),  // 0xaf
    M3OP_F( "i64.trunc_s/f64",  0,  i_64,   d_convertOpList (i64_Trunc_f64),        Compile_Convert ),  // 0xb0
    M3OP_F( "i64.trunc_u/f64",  0,  i_64,   d_convertOpList (u64_Trunc_f64),        Compile_Convert ),  // 0xb1

    M3OP_F( "f32.convert_s/i32",0,  f_32,   d_convertOpList (f32_Convert_i32),      Compile_Convert ),  // 0xb2
    M3OP_F( "f32.convert_u/i32",0,  f_32,   d_convertOpList (f32_Convert_u32),      Compile_Convert ),  // 0xb3
    M3OP_F( "f32.convert_s/i64",0,  f_32,   d_convertOpList (f32_Convert_i64),      Compile_Convert ),  // 0xb4
    M3OP_F( "f32.convert_u/i64",0,  f_32,   d_convertOpList (f32_Convert_u64),      Compile_Convert ),  // 0xb5

    M3OP_F( "f32.demote/f64",   0,  f_32,   d_unaryOpList (f32, Demote_f64),        NULL    ),          // 0xb6

    M3OP_F( "f64.convert_s/i32",0,  f_64,   d_convertOpList (f64_Convert_i32),      Compile_Convert ),  // 0xb7
    M3OP_F( "f64.convert_u/i32",0,  f_64,   d_convertOpList (f64_Convert_u32),      Compile_Convert ),  // 0xb8
    M3OP_F( "f64.convert_s/i64",0,  f_64,   d_convertOpList (f64_Convert_i64),      Compile_Convert ),  // 0xb9
    M3OP_F( "f64.convert_u/i64",0,  f_64,   d_convertOpList (f64_Convert_u64),      Compile_Convert ),  // 0xba

    M3OP_F( "f64.promote/f32",  0,  f_64,   d_unaryOpList (f64, Promote_f32),       NULL    ),          // 0xbb

    M3OP_F( "i32.reinterpret/f32",0,i_32,   d_convertOpList (i32_Reinterpret_f32),  Compile_Convert ),  // 0xbc
    M3OP_F( "i64.reinterpret/f64",0,i_64,   d_convertOpList (i64_Reinterpret_f64),  Compile_Convert ),  // 0xbd
    M3OP_F( "f32.reinterpret/i32",0,f_32,   d_convertOpList (f32_Reinterpret_i32),  Compile_Convert ),  // 0xbe
    M3OP_F( "f64.reinterpret/i64",0,f_64,   d_convertOpList (f64_Reinterpret_i64),  Compile_Convert ),  // 0xbf

    M3OP( "i32.extend8_s",       0,  i_32,   d_unaryOpList (i32, Extend8_s),        NULL    ),          // 0xc0
    M3OP( "i32.extend16_s",      0,  i_32,   d_unaryOpList (i32, Extend16_s),       NULL    ),          // 0xc1
    M3OP( "i64.extend8_s",       0,  i_64,   d_unaryOpList (i64, Extend8_s),        NULL    ),          // 0xc2
    M3OP( "i64.extend16_s",      0,  i_64,   d_unaryOpList (i64, Extend16_s),       NULL    ),          // 0xc3
    M3OP( "i64.extend32_s",      0,  i_64,   d_unaryOpList (i64, Extend32_s),       NULL    ),          // 0xc4

# ifdef DEBUG // for codepage logging:
#   define d_m3DebugOp(OP) M3OP (#OP, 0, none, { op_##OP })
#   define d_m3DebugTypedOp(OP) M3OP (#OP, 0, none, { op_##OP##_i32, op_##OP##_i64, op_##OP##_f32, op_##OP##_f64, })

    d_m3DebugOp (Entry),            d_m3DebugOp (Compile),      d_m3DebugOp (End),

    d_m3DebugOp (Unsupported),
    d_m3DebugOp (CallRawFunction),

    d_m3DebugOp (GetGlobal_s32),    d_m3DebugOp (GetGlobal_s64),    d_m3DebugOp (ContinueLoop),     d_m3DebugOp (ContinueLoopIf),

    d_m3DebugOp (CopySlot_32),      d_m3DebugOp (PreserveCopySlot_32),
    d_m3DebugOp (CopySlot_64),      d_m3DebugOp (PreserveCopySlot_64),

    d_m3DebugOp (i32_BranchIf_rs),  d_m3DebugOp (i32_BranchIf_ss),  d_m3DebugOp (i64_BranchIf_rs),  d_m3DebugOp (i64_BranchIf_ss),

    d_m3DebugOp (Select_i32_rss),   d_m3DebugOp (Select_i32_srs),   d_m3DebugOp (Select_i32_ssr),   d_m3DebugOp (Select_i32_sss),
    d_m3DebugOp (Select_i64_rss),   d_m3DebugOp (Select_i64_srs),   d_m3DebugOp (Select_i64_ssr),   d_m3DebugOp (Select_i64_sss),

    d_m3DebugOp (Select_f32_sss),   d_m3DebugOp (Select_f32_srs),   d_m3DebugOp (Select_f32_ssr),
    d_m3DebugOp (Select_f32_rss),   d_m3DebugOp (Select_f32_rrs),   d_m3DebugOp (Select_f32_rsr),

    d_m3DebugOp (Select_f64_sss),   d_m3DebugOp (Select_f64_srs),   d_m3DebugOp (Select_f64_ssr),
    d_m3DebugOp (Select_f64_rss),   d_m3DebugOp (Select_f64_rrs),   d_m3DebugOp (Select_f64_rsr),

    d_m3DebugTypedOp (SetGlobal),   d_m3DebugOp (SetGlobal_s32),    d_m3DebugOp (SetGlobal_s64),

    d_m3DebugTypedOp (SetRegister), d_m3DebugTypedOp (SetSlot),     d_m3DebugTypedOp (PreserveSetSlot),
# endif

# ifdef d_m3CompileExtendedOpcode
    [0xFC] = M3OP( "0xFC", 0, c_m3Type_unknown,   d_emptyOpList,  Compile_ExtendedOpcode ),
# endif

# ifdef DEBUG
    M3OP( "termination", 0, c_m3Type_unknown ) // for find_operation_info
# endif
};

const M3OpInfo c_operationsFC [] =
{
    M3OP_F( "i32.trunc_s:sat/f32",0,  i_32,   d_convertOpList (i32_TruncSat_f32),        Compile_Convert ),  // 0x00
    M3OP_F( "i32.trunc_u:sat/f32",0,  i_32,   d_convertOpList (u32_TruncSat_f32),        Compile_Convert ),  // 0x01
    M3OP_F( "i32.trunc_s:sat/f64",0,  i_32,   d_convertOpList (i32_TruncSat_f64),        Compile_Convert ),  // 0x02
    M3OP_F( "i32.trunc_u:sat/f64",0,  i_32,   d_convertOpList (u32_TruncSat_f64),        Compile_Convert ),  // 0x03
    M3OP_F( "i64.trunc_s:sat/f32",0,  i_64,   d_convertOpList (i64_TruncSat_f32),        Compile_Convert ),  // 0x04
    M3OP_F( "i64.trunc_u:sat/f32",0,  i_64,   d_convertOpList (u64_TruncSat_f32),        Compile_Convert ),  // 0x05
    M3OP_F( "i64.trunc_s:sat/f64",0,  i_64,   d_convertOpList (i64_TruncSat_f64),        Compile_Convert ),  // 0x06
    M3OP_F( "i64.trunc_u:sat/f64",0,  i_64,   d_convertOpList (u64_TruncSat_f64),        Compile_Convert ),  // 0x07

# ifdef DEBUG
    M3OP( "termination", 0, c_m3Type_unknown ) // for find_operation_info
# endif
};

M3Result  Compile_BlockStatements  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    while (o->wasm < o->wasmEnd)
    {                                                                   emit_stack_dump (o);
        o->lastOpcodeStart = o->wasm;
        m3opcode_t opcode = * (o->wasm++);                              log_opcode (o, opcode);

#ifndef d_m3CompileExtendedOpcode
        if (UNLIKELY(opcode == 0xFC)) {
            opcode = (opcode << 8) | (* (o->wasm++));
        }
#endif

        IM3OpInfo opinfo = GetOpInfo(opcode);
        _throwif(m3Err_unknownOpcode, opinfo == NULL);

        if (opinfo->compiler) {
            result = (* opinfo->compiler) (o, opcode);
        } else {
            result = Compile_Operator(o, opcode);
        }

        o->previousOpcode = opcode;                             //                      m3logif (stack, dump_type_stack (o))

        if (o->stackIndex > d_m3MaxFunctionStackHeight)         // TODO: is this only place to check?
            result = m3Err_functionStackOverflow;

        if (result)
            break;

        if (opcode == c_waOp_end or opcode == c_waOp_else)
            break;
    }
_catch:
    return result;
}


M3Result  ValidateBlockEnd  (IM3Compilation o, bool * o_copyStackTopToRegister)
{
    M3Result result = m3Err_none;

    * o_copyStackTopToRegister = false;

    u8 valueType = GetSingleRetType(o->block.type);

    if (valueType != c_m3Type_none)
    {
        if (IsStackPolymorphic (o))
        {
_           (UnwindBlockStack (o));
_           (PushRegister (o, valueType));
        }
        else
        {
            i16 initStackIndex = o->block.initStackIndex;

            if (o->block.depth > 0 and initStackIndex != o->stackIndex)
            {
                if (o->stackIndex == initStackIndex + 1)
                {
                    * o_copyStackTopToRegister = IsStackTopInSlot (o);
                }
                else _throw ("unexpected block stack offset");
            }
        }
    }
    else
_       (UnwindBlockStack (o));

    _catch: return result;
}


M3Result  CompileBlock  (IM3Compilation o, /*pc_t * o_startPC,*/ IM3FuncType i_blockType, u8 i_blockOpcode)
{
    M3Result result;                                                                    d_m3Assert (not IsRegisterAllocated (o, 0));
                                                                                        d_m3Assert (not IsRegisterAllocated (o, 1));
    M3CompilationScope outerScope = o->block;
    M3CompilationScope * block = & o->block;

    block->outer            = & outerScope;
    block->pc               = GetPagePC (o->page);
    block->patches          = NULL;
    block->type             = i_blockType;
    block->initStackIndex   = o->stackIndex;
    block->depth            ++;
//    block->loopDepth        += (i_blockOpcode == c_waOp_loop);
    block->opcode           = i_blockOpcode;

_   (Compile_BlockStatements (o));

    bool moveStackTopToRegister;
_   (ValidateBlockEnd (o, & moveStackTopToRegister));

    if (moveStackTopToRegister)
_       (MoveStackTopToRegister (o));

    PatchBranches (o);

    o->block = outerScope;

    _catch: return result;
}


M3Result  CompileLocals  (IM3Compilation o)
{
    M3Result result;

    u32 numLocalBlocks;
_   (ReadLEB_u32 (& numLocalBlocks, & o->wasm, o->wasmEnd));

    for (u32 l = 0; l < numLocalBlocks; ++l)
    {
        u32 varCount;
        i8 waType;
        u8 localType;

_       (ReadLEB_u32 (& varCount, & o->wasm, o->wasmEnd));
_       (ReadLEB_i7 (& waType, & o->wasm, o->wasmEnd));
_       (NormalizeType (& localType, waType));
                                                                                                m3log (compile, "pushing locals. count: %d; type: %s", varCount, c_waTypes [localType]);
        while (varCount--)
_           (PushAllocatedSlot (o, localType));
    }

    _catch: return result;
}


M3Result  Compile_ReserveConstants  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    // in the interest of speed, this blindly scans the Wasm code looking for any byte
    // that looks like an const opcode.
    u32 numConstantSlots = 0;

    bytes_t wa = o->wasm;
    while (wa < o->wasmEnd)
    {
        u8 code = * wa++;

        if (code == 0x41 or code == 0x43)       // i32, f32
            numConstantSlots += 1;
        else if (code == 0x42 or code == 0x44)  // i64, f64
            numConstantSlots += GetTypeNumSlots (c_m3Type_i64);
    }                                                                                           m3log (compile, "estimated constant slots: %d", numConstantSlots)

    // if constants overflow their reserved stack space, the compiler simply emits op_Const
    // operations as needed. Compiled expressions (global inits) don't pass through this
    // ReserveConstants function and thus always produce inline constants.
    numConstantSlots = M3_MIN (numConstantSlots, d_m3MaxConstantTableSize);

    o->firstDynamicSlotIndex = o->firstConstSlotIndex + numConstantSlots;

    if (o->firstDynamicSlotIndex >= d_m3MaxFunctionSlots)
        result = m3Err_functionStackOverflow;

    return result;
}


void  SetupCompilation (IM3Compilation o)
{
    IM3BranchPatch patches = o->releasedPatches;
    memset (o, 0x0, sizeof (M3Compilation));
    o->releasedPatches = patches;
}


M3Result  Compile_Function  (IM3Function io_function)
{
    IM3FuncType ft = io_function->funcType;

    M3Result result = m3Err_none;                                     m3log (compile, "compiling: '%s'; wasm-size: %d; numArgs: %d; return: %s",
                                                                           m3_GetFunctionName(io_function), (u32) (io_function->wasmEnd - io_function->wasm), GetFunctionNumArgs (io_function), c_waTypes [GetSingleRetType(ft)]);
    IM3Runtime runtime = io_function->module->runtime;

    IM3Compilation o = & runtime->compilation;
    SetupCompilation (o);

    o->runtime  = runtime;
    o->module   = io_function->module;
    o->function = io_function;
    o->wasm     = io_function->wasm;
    o->wasmEnd  = io_function->wasmEnd;

_try {
_   (AcquireCompilationCodePage (o, & o->page));

    pc_t pc = GetPagePC (o->page);

    // push the arg types to the type stack
    o->block.type = ft;

    // all args are 64-bit aligned
    const u32 argSlotCount = sizeof (u64) / sizeof (m3slot_t);
    u32 numArgs = GetFunctionNumArgs (o->function);
    u32 numRets = GetFunctionNumReturns(o->function);

    for (u32 i = 0; i < numArgs; ++i)
    {
        u8 type = ft->types [numRets + i];
_       (PushAllocatedSlot (o, type));

        if (i < numArgs - 1)
        {
            // prevent allocator fill-in
            o->firstDynamicSlotIndex += argSlotCount;
        }
        else
        {
            // final arg only allocates its natural width when using 32-bit slots
            o->firstDynamicSlotIndex += GetTypeNumSlots (type);
        }
    }

    o->function->numArgSlots = o->firstLocalSlotIndex = o->firstDynamicSlotIndex;
_   (CompileLocals (o));

    u16 maxSlot = GetMaxUsedSlotPlusOne (o);

    o->function->numLocalBytes = (maxSlot - o->firstLocalSlotIndex) * sizeof (m3slot_t);

    o->firstConstSlotIndex = o->maxConstSlotIndex = maxSlot;

    // ReserveConstants initializes o->firstDynamicSlotIndex
_   (Compile_ReserveConstants (o));

    // start tracking the max stack used (Push() also updates this value) so that op_Entry can precisely detect stack overflow
    o->function->maxStackSlots = o->maxAllocatedSlotPlusOne = o->firstDynamicSlotIndex;

    o->block.initStackIndex = o->firstDynamicStackIndex = o->stackIndex;                           m3log (compile, "start stack index: %d", (u32) o->firstDynamicStackIndex);

_   (EmitOp (o, op_Entry));
    EmitPointer (o, io_function);

_   (Compile_BlockStatements (o));

    io_function->compiled = pc;

    u32 numConstantSlots = o->maxConstSlotIndex - o->firstConstSlotIndex;       m3log (compile, "unique constant slots: %d; unused slots: %d", numConstantSlots, o->firstDynamicSlotIndex - o->maxConstSlotIndex);

    io_function->numConstantBytes = numConstantSlots * sizeof (m3slot_t);

    if (numConstantSlots)
    {
_       (m3CopyMem (& io_function->constants, o->constants, io_function->numConstantBytes));
    }

} _catch:

    ReleaseCompilationCodePage (o);

    return result;
}

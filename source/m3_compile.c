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

static const IM3Operation c_setSetOps [] = { NULL, op_SetSlot_i32, op_SetSlot_i64, op_SetSlot_f32, op_SetSlot_f64 };

#define d_indent "     | "

// just want less letter and numbers to stare at down the way in the compiler table
#define i_32    c_m3Type_i32
#define i_64    c_m3Type_i64
#define f_32    c_m3Type_f32
#define f_64    c_m3Type_f64
#define none    c_m3Type_none
#define any     (u8)-1


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


u32 GetTypeNumSlots (u8 i_type)
{
    return Is64BitType (i_type) ? 1 : 1;
}

i16  GetStackTopIndex  (IM3Compilation o)
{
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


u8  GetBlockType  (IM3Compilation o)
{
    return o->block.type;
}


bool  BlockHasType  (IM3Compilation o)
{
    return GetBlockType (o) != c_m3Type_none;
}


i16  GetNumBlockValues  (IM3Compilation o)
{
    return o->stackIndex - o->block.initStackIndex;
}


bool  IsStackTopInRegister  (IM3Compilation o)
{
    i16 i = GetStackTopIndex (o);               d_m3Assert (i >= 0 or IsStackPolymorphic (o));

    if (i >= 0)
    {
        return (o->wasmStack [i] >= d_m3Reg0SlotAlias);
    }
    else return false;
}


bool  IsStackTopInSlot  (IM3Compilation o)
{
    return not IsStackTopInRegister (o);
}


static const u16 c_slotUnused = 0xffff;

u16  GetStackTopSlotIndex  (IM3Compilation o)
{
    i16 i = GetStackTopIndex (o);               d_m3Assert (i >= 0 or IsStackPolymorphic (o));

    u16 slot = c_slotUnused;

    if (i >= 0)
        slot = o->wasmStack [i];

    return slot;
}


bool  IsValidSlot  (u16 i_slot)
{
    return (i_slot < d_m3MaxFunctionStackHeight);
}


bool  IsStackTopMinus1InRegister  (IM3Compilation o)
{
    i16 i = GetStackTopIndex (o);

    if (i > 0)
    {
        return (o->wasmStack [i - 1] >= d_m3Reg0SlotAlias);
    }
    else return false;
}


void  MarkSlotAllocated  (IM3Compilation o, u16 i_slot)
{                                                                   d_m3Assert (o->m3Slots [i_slot] == 0); // shouldn't be already allocated
    o->m3Slots [i_slot] = 1;
    o->numAllocatedExecSlots++;
}


bool  AllocateSlots  (IM3Compilation o, u16 * o_execSlot, u8 i_type)
{
    bool found = false;
    
    // search for empty slot in the execution stack
    i16 i = o->firstSlotIndex;
    while (i < d_m3MaxFunctionStackHeight)
    {
        if (o->m3Slots [i] == 0)
        {
            MarkSlotAllocated (o, i);
            * o_execSlot = i;

            found = true;
            break;
        }

        ++i;
    }

    return found;
}


M3Result  IncrementSlotUsageCount  (IM3Compilation o, u16 i_slot)
{                                                                                       d_m3Assert (i_slot < d_m3MaxFunctionStackHeight);
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
{                                                                                       d_m3Assert (i_slotIndex >= o->firstSlotIndex);
                                                                                        d_m3Assert (o->m3Slots [i_slotIndex]);
    for (u32 i = 0; i < GetTypeNumSlots (i_type); ++i, ++i_slotIndex)
    {
        if (-- o->m3Slots [i_slotIndex] == 0)
            o->numAllocatedExecSlots--;
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


u16  GetMaxExecSlot  (IM3Compilation o)
{
    u16 i = o->firstSlotIndex;

    u32 allocated = o->numAllocatedExecSlots;

    while (i < d_m3MaxFunctionStackHeight)
    {
        if (allocated == 0)
            break;

        if (o->m3Slots [i])
            --allocated;

        ++i;
    }

    return i;
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
        u16 slot;
        if (AllocateSlots (o, & slot, type))
        {
            o->wasmStack [stackIndex] = slot;

_           (EmitOp (o, c_setSetOps [type]));
            EmitSlotOffset (o, slot);
        }
        else _throw (m3Err_functionStackOverflow);
    }

    _catch: return result;
}


// all values must be in slots befor entering loop, if, and else blocks
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


M3Result  Push  (IM3Compilation o, u8 i_type, i16 i_location)
{
    M3Result result = m3Err_none;

    u16 stackIndex = o->stackIndex++;                                       // printf ("push: %d\n", (i32) i);

    if (stackIndex < d_m3MaxFunctionStackHeight)
    {
        if (o->function)
        {
            // op_Entry uses this value to track and detect stack overflow
            if (stackIndex > o->function->maxStackSlots)
                o->function->maxStackSlots = stackIndex;
        }

        o->wasmStack        [stackIndex] = i_location;
        o->typeStack        [stackIndex] = i_type;

        if (IsRegisterLocation (i_location))
        {
            u32 regSelect = IsFpRegisterLocation (i_location);
            AllocateRegister (o, regSelect, stackIndex);
        }                                                                   m3logif (stack, dump_type_stack (o))
    }
    else result = m3Err_functionStackOverflow;

    return result;
}


M3Result  PushRegister  (IM3Compilation o, u8 i_type)
{
    i16 location = IsFpType (i_type) ? d_m3Fp0SlotAlias : d_m3Reg0SlotAlias;            d_m3Assert (i_type or IsStackPolymorphic (o));
    return Push (o, i_type, location);
}


M3Result  Pop  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    if (o->stackIndex > o->block.initStackIndex)
    {
        o->stackIndex--;                                                //  printf ("pop: %d\n", (i32) o->stackIndex);

        i16 location = o->wasmStack [o->stackIndex];
        u8 type = o->typeStack [o->stackIndex];

        if (IsRegisterLocation (location))
        {
            u32 regSelect = IsFpRegisterLocation (location);
            DeallocateRegister (o, regSelect);
        }
        else if (location >= o->firstSlotIndex)
        {
            DeallocateSlot (o, location, type);
        }

        m3logif (stack, dump_type_stack (o))
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

    u16 slot;

    if (AllocateSlots (o, & slot, i_type))
    {
_       (Push (o, i_type, slot));

        if (i_doEmit)
            EmitSlotOffset (o, slot);
    }
    else result = m3Err_functionStackOverflow;

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

    i16 location = -1;

    u32 numConstants = o->constSlotIndex - o->firstConstSlotIndex;

    // search for duplicate matching constant slot to reuse
    for (u32 i = 0; i < numConstants; ++i)
    {
        if (o->constants [i] == i_word)
        {
            location = o->firstConstSlotIndex + i;
_           (Push (o, i_type, location));
            break;
        }
    }

    if (location < 0)
    {
        if (o->constSlotIndex < o->firstSlotIndex)
        {
            o->constants [numConstants] = i_word;
            location = o->constSlotIndex++;

_           (Push (o, i_type, location));
        }
        else
        {
_           (EmitOp (o, op_Const));
            EmitConstant64 (o, i_word);
_           (PushAllocatedSlotAndEmit (o, i_type));
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


M3Result CopyTopSlot (IM3Compilation o, u16 i_destSlot)
{
    M3Result result = m3Err_none;

    IM3Operation op;

    u8 type = GetStackTopType (o);

    if (IsStackTopInRegister (o))
    {
        op = c_setSetOps [type];
    }
    else op = Is64BitType (type) ? op_CopySlot_64 : op_CopySlot_32;

_   (EmitOp (o, op));
    EmitSlotOffset (o, i_destSlot);

    if (IsStackTopInSlot (o))
        EmitSlotOffset (o, GetStackTopSlotIndex (o));

    _catch: return result;
}


// a copy-on-write strategy is used with locals. when a get local occurs, it's not copied anywhere. the stack
// entry just has a index pointer to that local memory slot.
// then, when a previously referenced local is set, the current value needs to be preserved for those references

M3Result  PreservedCopyTopSlot  (IM3Compilation o, u16 i_destSlot, u16 i_preserveSlot)
{
    M3Result result = m3Err_none;             d_m3Assert (i_destSlot != i_preserveSlot);

    IM3Operation op;

    u8 type = GetStackTopType (o);
    
    if (IsStackTopInRegister (o))
    {
        const IM3Operation c_preserveSetSlot [] = { NULL, op_PreserveSetSlot_i32, op_PreserveSetSlot_i64, op_PreserveSetSlot_f32, op_PreserveSetSlot_f64 };
        
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

        static const IM3Operation setRegisterOps [] = { NULL, op_SetRegister_i32, op_SetRegister_i64, op_SetRegister_f32, op_SetRegister_f64 };

        IM3Operation op = setRegisterOps [type];

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
M3Result  FindReferencedLocalWithinCurrentBlock  (IM3Compilation o, u16 * o_preservedSlotIndex, u32 i_localIndex)
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
    
    * o_preservedSlotIndex = (u16) i_localIndex;

    for (u32 i = startIndex; i < o->stackIndex; ++i)
    {
        if (o->wasmStack [i] == i_localIndex)
        {
            if (* o_preservedSlotIndex == i_localIndex)
            {
                u8 localType = GetStackBottomType (o, i_localIndex);
                
                if (not AllocateSlots (o, o_preservedSlotIndex, localType))
                    _throw (m3Err_functionStackOverflow);
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


M3Result  Compile_Const_i32  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    i32 value;
_   (ReadLEB_i32 (& value, & o->wasm, o->wasmEnd));             m3log (compile, d_indent "%s (const i32 = %" PRIi32 ")", get_indention_string (o), value);

_   (PushConst (o, value, c_m3Type_i32));

    _catch: return result;
}


M3Result  Compile_Const_i64  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    i64 value;
_   (ReadLEB_i64 (& value, & o->wasm, o->wasmEnd));             m3log (compile, d_indent "%s (const i64 = %" PRIi64 ")", get_indention_string (o), value);

_   (PushConst (o, value, c_m3Type_i64));

    _catch: return result;
}


M3Result  Compile_Const_f32  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    f32 value;

_   (Read_f32 (& value, & o->wasm, o->wasmEnd));                m3log (compile, d_indent "%s (const f32 = %f)", get_indention_string (o), value);

    union { u64 u; f32 f; } union64;
    union64.f = value;

_   (PushConst (o, union64.u, c_m3Type_f32));

    _catch: return result;
}


M3Result  Compile_Const_f64  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    f64 value;

_   (Read_f64 (& value, & o->wasm, o->wasmEnd));                m3log (compile, d_indent "%s (const f64 = %lf)", get_indention_string (o), value);

    union { u64 u; f64 f; } union64;
    union64.f = value;

_   (PushConst (o, union64.u, c_m3Type_f64));

    _catch: return result;
}


M3Result  Compile_Return  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    bool hasReturn = GetFunctionNumReturns (o->function);

    if (hasReturn)
    {
_       (ReturnStackTop (o));
_       (Pop (o));
    }

_   (EmitOp (o, op_Return));

    o->block.isPolymorphic = true;

    _catch: return result;
}


M3Result  Compile_End  (IM3Compilation o, u8 i_opcode)
{
    M3Result result = m3Err_none;

    // function end:
    if (o->block.depth == 0)
    {
        u8 valueType = o->block.type;

        if (valueType)
        {
            // if there are branches to the function end, then their values are in a register
            // if the block happens to have its top in a register too, then we can patch the branch
            // to here. Otherwise, an ReturnStackTop is appended to the end of the function (at B) and
            // branches patched there.
            if (IsStackTopInRegister (o))
                PatchBranches (o);

_             (ReturnStackTop (o));
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



M3Result  Compile_SetLocal  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    u32 localSlot;
_   (ReadLEB_u32 (& localSlot, & o->wasm, o->wasmEnd));             //  printf ("--- set local: %d \n", localSlot);

    if (localSlot < GetFunctionNumArgsAndLocals (o->function))
    {
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


M3Result  Compile_GetLocal  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

_try {
    u32 localIndex;
_   (ReadLEB_u32 (& localIndex, & o->wasm, o->wasmEnd));

    u8 type = o->typeStack [localIndex];
_   (Push (o, type, localIndex));

    } _catch: return result;
}


M3Result  Compile_GetGlobal  (IM3Compilation o, M3Global * i_global)
{
    M3Result result;

_   (EmitOp (o, op_GetGlobal));
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
            const IM3Operation c_setGlobalOps [] = { NULL, op_SetGlobal_i32, op_SetGlobal_i64, op_SetGlobal_f32, op_SetGlobal_f64 };

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


M3Result  Compile_GetSetGlobal  (IM3Compilation o, u8 i_opcode)
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
    else result = ErrorCompile (m3Err_globaIndexOutOfBounds, o, "");

    _catch: return result;
}


M3Result  Compile_Branch  (IM3Compilation o, u8 i_opcode)
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
        u8 valueType = scope->type;

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

                    const IM3Operation ifOps [2][2] = { { op_i32_BranchIf_ss, op_i32_BranchIf_rs }, { op_i64_BranchIf_ss, op_i64_BranchIf_rs } };

                    op = ifOps [valueType - c_m3Type_i32] [conditionInRegister];
                }
            }
        }
        else
        {
            op = op_Branch;

            if (valueType != c_m3Type_none and not IsStackPolymorphic (o))
_               (MoveStackTopToRegister (o));

//_           (UnwindBlockStack (o));

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




M3Result  Compile_BranchTable  (IM3Compilation o, u8 i_opcode)
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
    EmitConstant (o, targetCount);

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
            IM3CodePage continueOpPage = AcquireCodePage (o->runtime);

            if (continueOpPage)
            {
                pc_t startPC = GetPagePC (continueOpPage);
                EmitPointer (o, startPC);

                IM3CodePage savedPage = o->page;
                o->page = continueOpPage;

_               (EmitOp (o, op_ContinueLoop));
                EmitPointer (o, scope->pc);

                ReleaseCompilationCodePage (o);     // FIX: continueOpPage can get lost if thrown
                o->page = savedPage;
            }
            else _throw (m3Err_mallocFailedCodePage);
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

//_   (UnwindBlockStack (o));
    } _catch: return result;
}


M3Result  CompileCallArgsReturn  (IM3Compilation o, u16 * o_stackOffset, IM3FuncType i_type, bool i_isIndirect)
{
    M3Result result = m3Err_none;

_try {
    // force use of at least one stack slot; this is to help ensure
    // the m3 stack overflows (and traps) before the native stack can overflow.
    // e.g. see Wasm spec test 'runaway' in call.wast
    u16 execTop = GetMaxExecSlot (o);
    if (execTop == 0)
        execTop = 1;

    * o_stackOffset = execTop;

    u32 numArgs = i_type->numArgs + i_isIndirect;
    u16 argTop = execTop + numArgs;

    while (numArgs--)
    {
_       (CopyTopSlot (o, --argTop));
_       (Pop (o));
    }

    i32 numReturns = i_type->returnType ? 1 : 0;

    if (numReturns)
    {
        MarkSlotAllocated (o, execTop);
_       (Push (o, i_type->returnType, execTop));
    }

    } _catch: return result;
}


M3Result  Compile_Call  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

_try {
    u32 functionIndex;
_   (ReadLEB_u32 (& functionIndex, & o->wasm, o->wasmEnd));

    IM3Function function = Module_GetFunction (o->module, functionIndex);

    if (function)
    {                                                                   m3log (compile, d_indent "%s (func= '%s'; args= %d)",
                                                                                get_indention_string (o), GetFunctionName (function), function->funcType->numArgs);
        if (function->module)
        {
            // OPTZ: could avoid arg copy when args are already sequential and at top

            u16 slotTop;
_           (CompileCallArgsReturn (o, & slotTop, function->funcType, false));

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
            result = ErrorCompile (m3Err_functionImportMissing, o, "'%s.%s'", GetFunctionImportModuleName (function), GetFunctionName (function));
        }
    }
    else result = m3Err_functionLookupFailed;

    } _catch: return result;
}


M3Result  Compile_CallIndirect  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    u32 typeIndex;
_   (ReadLEB_u32 (& typeIndex, & o->wasm, o->wasmEnd));

    i8 reserved;
_   (ReadLEB_i7 (& reserved, & o->wasm, o->wasmEnd));

    if (typeIndex < o->module->numFuncTypes)
    {
        u16 execTop;
        IM3FuncType type = & o->module->funcTypes [typeIndex];
_       (CompileCallArgsReturn (o, & execTop, type, true));

_       (EmitOp     (o, op_CallIndirect));
        EmitPointer (o, o->module);
        EmitPointer (o, type);              // TODO: unify all types in M3Environment
        EmitSlotOffset  (o, execTop);
    }
    else _throw ("function type index out of range");

    _catch: return result;
}


M3Result  Compile_Memory_Current  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    i8 reserved;
_   (ReadLEB_i7 (& reserved, & o->wasm, o->wasmEnd));

_   (EmitOp     (o, op_MemCurrent));

_   (PushRegister (o, c_m3Type_i32));

    _catch: return result;
}


M3Result  Compile_Memory_Grow  (IM3Compilation o, u8 i_opcode)
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


M3Result  ReadBlockType  (IM3Compilation o, u8 * o_blockType)
{
    M3Result result;                                                        d_m3Assert (o_blockType);

    i8 type;

_   (ReadLEB_i7 (& type, & o->wasm, o->wasmEnd));
_   (NormalizeType (o_blockType, type));                                if (* o_blockType)  m3log (compile, d_indent "%s (block_type: 0x%02x normalized: %d)",
                                                                                                   get_indention_string (o), (u32) (u8) type, (u32) * o_blockType);
    _catch: return result;
}


// This preemptively preserves args and locals on the stack that might be written-to in the subsequent block
// (versus the COW strategy that happens in SetLocal within a block).  Initially, I thought I'd have to be clever and
// retroactively insert preservation code to avoid impacting general performance, but this compilation pattern doesn't
// really occur in compiled Wasm code, so PreserveArgsAndLocals generally does nothing. Still waiting on a real-world case!
M3Result  PreserveArgsAndLocals  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    if (o->stackIndex > o->firstSlotIndex)
    {
        u32 numArgsAndLocals = GetFunctionNumArgsAndLocals (o->function);
        
        for (u32 i = 0; i < numArgsAndLocals; ++i)
        {
            u16 preservedSlotIndex;
_           (FindReferencedLocalWithinCurrentBlock (o, & preservedSlotIndex, i));
            
            if (preservedSlotIndex != i)
            {
                u8 type = GetStackBottomType (o, i);
                IM3Operation op = Is64BitType (type) ? op_CopySlot_64 : op_CopySlot_32;
                
                EmitOp          (o, op);
                EmitSlotOffset  (o, preservedSlotIndex);
                EmitSlotOffset  (o, i);
            }
        }
    }
    
    _catch:
    return result;
}


M3Result  Compile_LoopOrBlock  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

_   (PreserveRegisters (o));
_   (PreserveArgsAndLocals (o));

    u8 blockType;
_   (ReadBlockType (o, & blockType));

    if (i_opcode == c_waOp_loop)
_       (EmitOp (o, op_Loop));

_   (CompileBlock (o, blockType, i_opcode));

    _catch: return result;
}


M3Result  CompileElseBlock  (IM3Compilation o, pc_t * o_startPC, u8 i_blockType)
{
    M3Result result;

    IM3CodePage elsePage = AcquireCodePage (o->runtime);

    if (elsePage)
    {
        * o_startPC = GetPagePC (elsePage);

        IM3CodePage savedPage = o->page;
        o->page = elsePage;

_       (CompileBlock (o, i_blockType, c_waOp_else));

_       (EmitOp (o, op_Branch));
        EmitPointer (o, GetPagePC (savedPage));

        ReleaseCompilationCodePage (o);

        o->page = savedPage;
    }
    else result = m3Err_mallocFailedCodePage;

    _catch:

    return result;
}


M3Result  Compile_If  (IM3Compilation o, u8 i_opcode)
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

    u8 blockType;
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


M3Result  Compile_Select  (IM3Compilation o, u8 i_opcode)
{
    static const IM3Operation intSelectOps [2] [4] =      { { op_Select_i32_rss, op_Select_i32_srs, op_Select_i32_ssr, op_Select_i32_sss },
                                                            { op_Select_i64_rss, op_Select_i64_srs, op_Select_i64_ssr, op_Select_i64_sss } };

    static const IM3Operation fpSelectOps [2] [2] [3] = { { { op_Select_f32_sss, op_Select_f32_srs, op_Select_f32_ssr },        // selector in slot
                                                            { op_Select_f32_rss, op_Select_f32_rrs, op_Select_f32_rsr } },      // selector in reg
                                                          { { op_Select_f64_sss, op_Select_f64_srs, op_Select_f64_ssr },        // selector in slot
                                                            { op_Select_f64_rss, op_Select_f64_rrs, op_Select_f64_rsr } } };    // selector in reg
    M3Result result = m3Err_none;

    u16 slots [3] = { c_slotUnused, c_slotUnused, c_slotUnused };

    u8 type = GetStackTopTypeAtOffset (o, 1); // get type of selection

    IM3Operation op = NULL;

    if (IsFpType (type))
    {
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

        // not consuming a fp reg, so preserve
        if (opIndex == 0)
_          (PreserveRegisterIfOccupied (o, type));

        op = fpSelectOps [type - c_m3Type_f32] [selectorInReg] [opIndex];
    }
    else if (IsIntType (type))
    {
        u32 opIndex = 3;  // op_Select_*_sss

        for (u32 i = 0; i < 3; ++i)
        {
            if (IsStackTopInRegister (o))
                opIndex = i;
            else
                slots [i] = GetStackTopSlotIndex (o);

_          (Pop (o));
        }

        // 'sss' operation doesn't consume a register, so might have to protected its contents
        if (opIndex == 3)
_          (PreserveRegisterIfOccupied (o, type));

        op = intSelectOps [type - c_m3Type_i32] [opIndex];
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


M3Result  Compile_Drop  (IM3Compilation o, u8 i_opcode)
{
    return Pop (o);
}


M3Result  Compile_Nop  (IM3Compilation o, u8 i_opcode)
{
    return m3Err_none;
}


M3Result  Compile_Unreachable  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

_   (AddTrapRecord (o));

_   (EmitOp (o, op_Unreachable));
    o->block.isPolymorphic = true;

    _catch:
    return result;
}


// TODO OPTZ: currently all stack slot indicies take up a full word, but
// dual stack source operands could be packed together
M3Result  Compile_Operator  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

    const M3OpInfo * op = & c_operations [i_opcode];

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


M3Result  Compile_Convert  (IM3Compilation o, u8 i_opcode)
{
    M3Result result = m3Err_none;

    const M3OpInfo * opInfo = & c_operations [i_opcode];

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



M3Result  Compile_Load_Store  (IM3Compilation o, u8 i_opcode)
{
    M3Result result;

_try {
    u32 alignHint, memoryOffset;

_   (ReadLEB_u32 (& alignHint, & o->wasm, o->wasmEnd));
_   (ReadLEB_u32 (& memoryOffset, & o->wasm, o->wasmEnd));
                                                                        m3log (compile, d_indent "%s (offset = %d)", get_indention_string (o), memoryOffset);
    const M3OpInfo * op = & c_operations [i_opcode];

    if (IsFpType (op->type)) // loading a float?
_       (PreserveRegisterIfOccupied (o, c_m3Type_f64));

_   (Compile_Operator (o, i_opcode));

    EmitConstant (o, memoryOffset);
}
    _catch: return result;
}


// d_singleOp macros aren't actually used by the compiler, just codepage decoding
#define d_singleOp(OP)                      { op_##OP,                  NULL,                       NULL,                       NULL }
#define d_emptyOpList()                     { NULL,                     NULL,                       NULL,                       NULL }
#define d_unaryOpList(TYPE, NAME)           { op_##TYPE##_##NAME##_r,   op_##TYPE##_##NAME##_s,     NULL,                       NULL }
#define d_binOpList(TYPE, NAME)             { op_##TYPE##_##NAME##_rs,  op_##TYPE##_##NAME##_sr,    op_##TYPE##_##NAME##_ss,    NULL }
#define d_storeFpOpList(TYPE, NAME)         { op_##TYPE##_##NAME##_rs,  op_##TYPE##_##NAME##_sr,    op_##TYPE##_##NAME##_ss,    op_##TYPE##_##NAME##_rr }
#define d_commutativeBinOpList(TYPE, NAME)  { op_##TYPE##_##NAME##_rs,  NULL,                       op_##TYPE##_##NAME##_ss,    NULL }
#define d_convertOpList(OP)                 { op_##OP##_r_r,            op_##OP##_r_s,              op_##OP##_s_r,              op_##OP##_s_s }


const M3OpInfo c_operations [] =
{
    M3OP( "unreachable",         0, none,   d_singleOp (Unreachable),           Compile_Unreachable ),  // 0x00
    M3OP( "nop",                 0, none,   d_emptyOpList(),                    Compile_Nop ),          // 0x01 .
    M3OP( "block",               0, none,   d_emptyOpList(),                    Compile_LoopOrBlock ),  // 0x02
    M3OP( "loop",                0, none,   d_singleOp (Loop),                  Compile_LoopOrBlock ),  // 0x03
    M3OP( "if",                 -1, none,   d_emptyOpList(),                    Compile_If ),           // 0x04
    M3OP( "else",                0, none,   d_emptyOpList(),                    Compile_Nop ),          // 0x05

    M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED,                          // 0x06 - 0x0a

    M3OP( "end",                 0, none,   d_emptyOpList(),                    Compile_End ),          // 0x0b
    M3OP( "br",                  0, none,   d_singleOp (Branch),                Compile_Branch ),       // 0x0c
    M3OP( "br_if",              -1, none,   { op_BranchIf_r, op_BranchIf_s },   Compile_Branch ),       // 0x0d
    M3OP( "br_table",           -1, none,   d_singleOp (BranchTable),           Compile_BranchTable ),  // 0x0e
    M3OP( "return",              0, any,    d_singleOp (Return),                Compile_Return ),       // 0x0f
    M3OP( "call",                0, any,    d_singleOp (Call),                  Compile_Call ),         // 0x10
    M3OP( "call_indirect",       0, any,    d_singleOp (CallIndirect),          Compile_CallIndirect ), // 0x11
    M3OP( "return_call",         0, any,    d_emptyOpList(),                    Compile_Call ),         // 0x12 TODO: Optimize
    M3OP( "return_call_indirect",0, any,    d_emptyOpList(),                    Compile_CallIndirect ), // 0x13

    M3OP_RESERVED,  M3OP_RESERVED,                                                                      // 0x14 - 0x15
    M3OP_RESERVED,  M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED,                                        // 0x16 - 0x19

    M3OP( "drop",               -1, none,   d_emptyOpList(),                    Compile_Drop ),         // 0x1a
    M3OP( "select",             -2, any,    d_emptyOpList(),                    Compile_Select  ),      // 0x1b

    M3OP_RESERVED,  M3OP_RESERVED, M3OP_RESERVED, M3OP_RESERVED,                                        // 0x1c - 0x1f

    M3OP( "local.get",          1,  any,    d_emptyOpList(),                    Compile_GetLocal ),     // 0x20
    M3OP( "local.set",          1,  none,   d_emptyOpList(),                    Compile_SetLocal ),     // 0x21
    M3OP( "local.tee",          0,  any,    d_emptyOpList(),                    Compile_SetLocal ),     // 0x22
    M3OP( "global.get",         1,  none,   d_singleOp (GetGlobal),             Compile_GetSetGlobal ), // 0x23
    M3OP( "global.set",         1,  none,   d_emptyOpList(),                    Compile_GetSetGlobal ), // 0x24

    M3OP_RESERVED,  M3OP_RESERVED, M3OP_RESERVED,                                                       // 0x25 - 0x27

    M3OP( "i32.load",           0,  i_32,   d_unaryOpList (i32, Load_i32),      Compile_Load_Store ),   // 0x28
    M3OP( "i64.load",           0,  i_64,   d_unaryOpList (i64, Load_i64),      Compile_Load_Store ),   // 0x29
    M3OP( "f32.load",           0,  f_32,   d_unaryOpList (f32, Load_f32),      Compile_Load_Store ),   // 0x2a
    M3OP( "f64.load",           0,  f_64,   d_unaryOpList (f64, Load_f64),      Compile_Load_Store ),   // 0x2b

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
    M3OP( "f32.store",          -2, none,   d_storeFpOpList (f32, Store_f32),   Compile_Load_Store ),   // 0x38
    M3OP( "f64.store",          -2, none,   d_storeFpOpList (f64, Store_f64),   Compile_Load_Store ),   // 0x39

    M3OP( "i32.store8",         -2, none,   d_binOpList (i32, Store_u8),        Compile_Load_Store ),   // 0x3a
    M3OP( "i32.store16",        -2, none,   d_binOpList (i32, Store_i16),       Compile_Load_Store ),   // 0x3b

    M3OP( "i64.store8",         -2, none,   d_binOpList (i64, Store_u8),        Compile_Load_Store ),   // 0x3c
    M3OP( "i64.store16",        -2, none,   d_binOpList (i64, Store_i16),       Compile_Load_Store ),   // 0x3d
    M3OP( "i64.store32",        -2, none,   d_binOpList (i64, Store_i32),       Compile_Load_Store ),   // 0x3e

    M3OP( "memory.current",     1,  i_32,   d_singleOp (MemCurrent),            Compile_Memory_Current ),   // 0x3f
    M3OP( "memory.grow",        1,  i_32,   d_singleOp (MemGrow),               Compile_Memory_Grow ),      // 0x40

    M3OP( "i32.const",          1,  i_32,   d_emptyOpList(),                    Compile_Const_i32 ),    // 0x41
    M3OP( "i64.const",          1,  i_64,   d_emptyOpList(),                    Compile_Const_i64 ),    // 0x42
    M3OP( "f32.const",          1,  f_32,   d_emptyOpList(),                    Compile_Const_f32 ),    // 0x43
    M3OP( "f64.const",          1,  f_64,   d_emptyOpList(),                    Compile_Const_f64 ),    // 0x44

    M3OP( "i32.eqz",            0,  i_32,   d_unaryOpList (i32, EqualToZero)        ),          // 0x45
    M3OP( "i32.eq",             -1, i_32,   d_commutativeBinOpList (i32, Equal)     ),          // 0x46
    M3OP( "i32.ne",             -1, i_32,   d_commutativeBinOpList (i32, NotEqual)  ),          // 0x47
    M3OP( "i32.lt_s",           -1, i_32,   d_binOpList (i32, LessThan)             ),          // 0x48
    M3OP( "i32.lt_u",           -1, i_32,   d_binOpList (u32, LessThan)             ),          // 0x49
    M3OP( "i32.gt_s",           -1, i_32,   d_binOpList (i32, GreaterThan)          ),          // 0x4a
    M3OP( "i32.gt_u",           -1, i_32,   d_binOpList (u32, GreaterThan)          ),          // 0x4b
    M3OP( "i32.le_s",           -1, i_32,   d_binOpList (i32, LessThanOrEqual)      ),          // 0x4c
    M3OP( "i32.le_u",           -1, i_32,   d_binOpList (u32, LessThanOrEqual)      ),          // 0x4d
    M3OP( "i32.ge_s",           -1, i_32,   d_binOpList (i32, GreaterThanOrEqual)   ),          // 0x4e
    M3OP( "i32.ge_u",           -1, i_32,   d_binOpList (u32, GreaterThanOrEqual)   ),          // 0x4f

    M3OP( "i64.eqz",            0,  i_32,   d_unaryOpList (i64, EqualToZero)        ),          // 0x50
    M3OP( "i64.eq",             -1, i_32,   d_commutativeBinOpList (i64, Equal)     ),          // 0x51
    M3OP( "i64.ne",             -1, i_32,   d_commutativeBinOpList (i64, NotEqual)  ),          // 0x52
    M3OP( "i64.lt_s",           -1, i_32,   d_binOpList (i64, LessThan)             ),          // 0x53
    M3OP( "i64.lt_u",           -1, i_32,   d_binOpList (u64, LessThan)             ),          // 0x54
    M3OP( "i64.gt_s",           -1, i_32,   d_binOpList (i64, GreaterThan)          ),          // 0x55
    M3OP( "i64.gt_u",           -1, i_32,   d_binOpList (u64, GreaterThan)          ),          // 0x56
    M3OP( "i64.le_s",           -1, i_32,   d_binOpList (i64, LessThanOrEqual)      ),          // 0x57
    M3OP( "i64.le_u",           -1, i_32,   d_binOpList (u64, LessThanOrEqual)      ),          // 0x58
    M3OP( "i64.ge_s",           -1, i_32,   d_binOpList (i64, GreaterThanOrEqual)   ),          // 0x59
    M3OP( "i64.ge_u",           -1, i_32,   d_binOpList (u64, GreaterThanOrEqual)   ),          // 0x5a

    M3OP( "f32.eq",             -1, i_32,   d_commutativeBinOpList (f32, Equal)     ),          // 0x5b
    M3OP( "f32.ne",             -1, i_32,   d_commutativeBinOpList (f32, NotEqual)  ),          // 0x5c
    M3OP( "f32.lt",             -1, i_32,   d_binOpList (f32, LessThan)             ),          // 0x5d
    M3OP( "f32.gt",             -1, i_32,   d_binOpList (f32, GreaterThan)          ),          // 0x5e
    M3OP( "f32.le",             -1, i_32,   d_binOpList (f32, LessThanOrEqual)      ),          // 0x5f
    M3OP( "f32.ge",             -1, i_32,   d_binOpList (f32, GreaterThanOrEqual)   ),          // 0x60

    M3OP( "f64.eq",             -1, i_32,   d_commutativeBinOpList (f64, Equal)     ),          // 0x61
    M3OP( "f64.ne",             -1, i_32,   d_commutativeBinOpList (f64, NotEqual)  ),          // 0x62
    M3OP( "f64.lt",             -1, i_32,   d_binOpList (f64, LessThan)             ),          // 0x63
    M3OP( "f64.gt",             -1, i_32,   d_binOpList (f64, GreaterThan)          ),          // 0x64
    M3OP( "f64.le",             -1, i_32,   d_binOpList (f64, LessThanOrEqual)      ),          // 0x65
    M3OP( "f64.ge",             -1, i_32,   d_binOpList (f64, GreaterThanOrEqual)   ),          // 0x66

    M3OP( "i32.clz",            0,  i_32,   d_unaryOpList (u32, Clz)                ),          // 0x67
    M3OP( "i32.ctz",            0,  i_32,   d_unaryOpList (u32, Ctz)                ),          // 0x68
    M3OP( "i32.popcnt",         0,  i_32,   d_unaryOpList (u32, Popcnt)             ),          // 0x69

    M3OP( "i32.add",            -1, i_32,   d_commutativeBinOpList (i32, Add)       ),          // 0x6a
    M3OP( "i32.sub",            -1, i_32,   d_binOpList (i32, Subtract)             ),          // 0x6b
    M3OP( "i32.mul",            -1, i_32,   d_commutativeBinOpList (i32, Multiply)  ),          // 0x6c
    M3OP( "i32.div_s",          -1, i_32,   d_binOpList (i32, Divide)               ),          // 0x6d
    M3OP( "i32.div_u",          -1, i_32,   d_binOpList (u32, Divide)               ),          // 0x6e
    M3OP( "i32.rem_s",          -1, i_32,   d_binOpList (i32, Remainder)            ),          // 0x6f
    M3OP( "i32.rem_u",          -1, i_32,   d_binOpList (u32, Remainder)            ),          // 0x70
    M3OP( "i32.and",            -1, i_32,   d_commutativeBinOpList (u32, And)       ),          // 0x71
    M3OP( "i32.or",             -1, i_32,   d_commutativeBinOpList (u32, Or)        ),          // 0x72
    M3OP( "i32.xor",            -1, i_32,   d_commutativeBinOpList (u32, Xor)       ),          // 0x73
    M3OP( "i32.shl",            -1, i_32,   d_binOpList (u32, ShiftLeft)            ),          // 0x74
    M3OP( "i32.shr_s",          -1, i_32,   d_binOpList (i32, ShiftRight)           ),          // 0x75
    M3OP( "i32.shr_u",          -1, i_32,   d_binOpList (u32, ShiftRight)           ),          // 0x76
    M3OP( "i32.rotl",           -1, i_32,   d_binOpList (u32, Rotl)                 ),          // 0x77
    M3OP( "i32.rotr",           -1, i_32,   d_binOpList (u32, Rotr)                 ),          // 0x78

    M3OP( "i64.clz",            0,  i_64,   d_unaryOpList (u64, Clz)                ),          // 0x79
    M3OP( "i64.ctz",            0,  i_64,   d_unaryOpList (u64, Ctz)                ),          // 0x7a
    M3OP( "i64.popcnt",         0,  i_64,   d_unaryOpList (u64, Popcnt)             ),          // 0x7b

    M3OP( "i64.add",            -1, i_64,   d_commutativeBinOpList (i64, Add)       ),          // 0x7c
    M3OP( "i64.sub",            -1, i_64,   d_binOpList (i64, Subtract)             ),          // 0x7d
    M3OP( "i64.mul",            -1, i_64,   d_commutativeBinOpList (i64, Multiply)  ),          // 0x7e
    M3OP( "i64.div_s",          -1, i_64,   d_binOpList (i64, Divide)               ),          // 0x7f
    M3OP( "i64.div_u",          -1, i_64,   d_binOpList (u64, Divide)               ),          // 0x80
    M3OP( "i64.rem_s",          -1, i_64,   d_binOpList (i64, Remainder)            ),          // 0x81
    M3OP( "i64.rem_u",          -1, i_64,   d_binOpList (u64, Remainder)            ),          // 0x82
    M3OP( "i64.and",            -1, i_64,   d_commutativeBinOpList (u64, And)       ),          // 0x83
    M3OP( "i64.or",             -1, i_64,   d_commutativeBinOpList (u64, Or)        ),          // 0x84
    M3OP( "i64.xor",            -1, i_64,   d_commutativeBinOpList (u64, Xor)       ),          // 0x85
    M3OP( "i64.shl",            -1, i_64,   d_binOpList (u64, ShiftLeft)            ),          // 0x86
    M3OP( "i64.shr_s",          -1, i_64,   d_binOpList (i64, ShiftRight)           ),          // 0x87
    M3OP( "i64.shr_u",          -1, i_64,   d_binOpList (u64, ShiftRight)           ),          // 0x88
    M3OP( "i64.rotl",           -1, i_64,   d_binOpList (u64, Rotl)                 ),          // 0x89
    M3OP( "i64.rotr",           -1, i_64,   d_binOpList (u64, Rotr)                 ),          // 0x8a

    M3OP( "f32.abs",            0,  f_32,   d_unaryOpList(f32, Abs)                 ),          // 0x8b
    M3OP( "f32.neg",            0,  f_32,   d_unaryOpList(f32, Negate)              ),          // 0x8c
    M3OP( "f32.ceil",           0,  f_32,   d_unaryOpList(f32, Ceil)                ),          // 0x8d
    M3OP( "f32.floor",          0,  f_32,   d_unaryOpList(f32, Floor)               ),          // 0x8e
    M3OP( "f32.trunc",          0,  f_32,   d_unaryOpList(f32, Trunc)               ),          // 0x8f
    M3OP( "f32.nearest",        0,  f_32,   d_unaryOpList(f32, Nearest)             ),          // 0x90
    M3OP( "f32.sqrt",           0,  f_32,   d_unaryOpList(f32, Sqrt)                ),          // 0x91

    M3OP( "f32.add",            -1, f_32,   d_commutativeBinOpList (f32, Add)       ),          // 0x92
    M3OP( "f32.sub",            -1, f_32,   d_binOpList (f32, Subtract)             ),          // 0x93
    M3OP( "f32.mul",            -1, f_32,   d_commutativeBinOpList (f32, Multiply)  ),          // 0x94
    M3OP( "f32.div",            -1, f_32,   d_binOpList (f32, Divide)               ),          // 0x95
    M3OP( "f32.min",            -1, f_32,   d_commutativeBinOpList (f32, Min)       ),          // 0x96
    M3OP( "f32.max",            -1, f_32,   d_commutativeBinOpList (f32, Max)       ),          // 0x97
    M3OP( "f32.copysign",       -1, f_32,   d_binOpList (f32, CopySign)             ),          // 0x98

    M3OP( "f64.abs",            0,  f_64,   d_unaryOpList(f64, Abs)                 ),          // 0x99
    M3OP( "f64.neg",            0,  f_64,   d_unaryOpList(f64, Negate)              ),          // 0x9a
    M3OP( "f64.ceil",           0,  f_64,   d_unaryOpList(f64, Ceil)                ),          // 0x9b
    M3OP( "f64.floor",          0,  f_64,   d_unaryOpList(f64, Floor)               ),          // 0x9c
    M3OP( "f64.trunc",          0,  f_64,   d_unaryOpList(f64, Trunc)               ),          // 0x9d
    M3OP( "f64.nearest",        0,  f_64,   d_unaryOpList(f64, Nearest)             ),          // 0x9e
    M3OP( "f64.sqrt",           0,  f_64,   d_unaryOpList(f64, Sqrt)                ),          // 0x9f

    M3OP( "f64.add",            -1, f_64,   d_commutativeBinOpList (f64, Add)       ),          // 0xa0
    M3OP( "f64.sub",            -1, f_64,   d_binOpList (f64, Subtract)             ),          // 0xa1
    M3OP( "f64.mul",            -1, f_64,   d_commutativeBinOpList (f64, Multiply)  ),          // 0xa2
    M3OP( "f64.div",            -1, f_64,   d_binOpList (f64, Divide)               ),          // 0xa3
    M3OP( "f64.min",            -1, f_64,   d_commutativeBinOpList (f64, Min)       ),          // 0xa4
    M3OP( "f64.max",            -1, f_64,   d_commutativeBinOpList (f64, Max)       ),          // 0xa5
    M3OP( "f64.copysign",       -1, f_64,   d_binOpList (f64, CopySign)             ),          // 0xa6

    M3OP( "i32.wrap/i64",       0,  i_32,   d_unaryOpList (i32, Wrap_i64)           ),                  // 0xa7
    M3OP( "i32.trunc_s/f32",    0,  i_32,   d_convertOpList (i32_Trunc_f32),        Compile_Convert ),  // 0xa8
    M3OP( "i32.trunc_u/f32",    0,  i_32,   d_convertOpList (u32_Trunc_f32),        Compile_Convert ),  // 0xa9
    M3OP( "i32.trunc_s/f64",    0,  i_32,   d_convertOpList (i32_Trunc_f64),        Compile_Convert ),  // 0xaa
    M3OP( "i32.trunc_u/f64",    0,  i_32,   d_convertOpList (u32_Trunc_f64),        Compile_Convert ),  // 0xab

    M3OP( "i64.extend_s/i32",   0,  i_64,   d_unaryOpList (i64, Extend_i32)         ),                  // 0xac
    M3OP( "i64.extend_u/i32",   0,  i_64,   d_unaryOpList (i64, Extend_u32)         ),                  // 0xad

    M3OP( "i64.trunc_s/f32",    0,  i_64,   d_convertOpList (i64_Trunc_f32),        Compile_Convert ),  // 0xae
    M3OP( "i64.trunc_u/f32",    0,  i_64,   d_convertOpList (u64_Trunc_f32),        Compile_Convert ),  // 0xaf
    M3OP( "i64.trunc_s/f64",    0,  i_64,   d_convertOpList (i64_Trunc_f64),        Compile_Convert ),  // 0xb0
    M3OP( "i64.trunc_u/f64",    0,  i_64,   d_convertOpList (u64_Trunc_f64),        Compile_Convert ),  // 0xb1

    M3OP( "f32.convert_s/i32",  0,  f_32,   d_convertOpList (f32_Convert_i32),      Compile_Convert ),  // 0xb2
    M3OP( "f32.convert_u/i32",  0,  f_32,   d_convertOpList (f32_Convert_u32),      Compile_Convert ),  // 0xb3
    M3OP( "f32.convert_s/i64",  0,  f_32,   d_convertOpList (f32_Convert_i64),      Compile_Convert ),  // 0xb4
    M3OP( "f32.convert_u/i64",  0,  f_32,   d_convertOpList (f32_Convert_u64),      Compile_Convert ),  // 0xb5

    M3OP( "f32.demote/f64",     0,  f_32,   d_unaryOpList (f32, Demote_f64)         ),                  // 0xb6

    M3OP( "f64.convert_s/i32",  0,  f_64,   d_convertOpList (f64_Convert_i32),      Compile_Convert ),  // 0xb7
    M3OP( "f64.convert_u/i32",  0,  f_64,   d_convertOpList (f64_Convert_u32),      Compile_Convert ),  // 0xb8
    M3OP( "f64.convert_s/i64",  0,  f_64,   d_convertOpList (f64_Convert_i64),      Compile_Convert ),  // 0xb9
    M3OP( "f64.convert_u/i64",  0,  f_64,   d_convertOpList (f64_Convert_u64),      Compile_Convert ),  // 0xba

    M3OP( "f64.promote/f32",    0,  f_64,   d_unaryOpList (f64, Promote_f32)        ),                  // 0xbb

    M3OP( "i32.reinterpret/f32", 0, i_32,   d_convertOpList (i32_Reinterpret_f32),  Compile_Convert ),  // 0xbc
    M3OP( "i64.reinterpret/f64", 0, i_64,   d_convertOpList (i64_Reinterpret_f64),  Compile_Convert ),  // 0xbd
    M3OP( "f32.reinterpret/i32", 0, f_32,   d_convertOpList (f32_Reinterpret_i32),  Compile_Convert ),  // 0xbe
    M3OP( "f64.reinterpret/i64", 0, f_64,   d_convertOpList (f64_Reinterpret_i64),  Compile_Convert ),  // 0xbf

    M3OP( "i32.extend8_s",       0,  i_32,   d_unaryOpList (i32, Extend8_s)          ),                 // 0xc0
    M3OP( "i32.extend16_s",      0,  i_32,   d_unaryOpList (i32, Extend16_s)         ),                 // 0xc1
    M3OP( "i64.extend8_s",       0,  i_64,   d_unaryOpList (i64, Extend8_s)          ),                 // 0xc2
    M3OP( "i64.extend16_s",      0,  i_64,   d_unaryOpList (i64, Extend16_s)         ),                 // 0xc3
    M3OP( "i64.extend32_s",      0,  i_64,   d_unaryOpList (i64, Extend32_s)         ),                 // 0xc4

# ifdef DEBUG // for codepage logging:
#   define d_m3DebugOp(OP) M3OP (#OP, 0, none, { op_##OP })
#   define d_m3DebugTypedOp(OP) M3OP (#OP, 0, none, { op_##OP##_i32, op_##OP##_i64, op_##OP##_f32, op_##OP##_f64, })

    d_m3DebugOp (Const),            d_m3DebugOp (Entry),                d_m3DebugOp (Compile),      d_m3DebugOp (End),

    d_m3DebugOp (ContinueLoop),     d_m3DebugOp (ContinueLoopIf),

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

    M3OP( "termination for find_operation_info ()", 0, c_m3Type_void )
    
# endif
};


M3Result  Compile_BlockStatements  (IM3Compilation o)
{
    M3Result result = m3Err_none;

    while (o->wasm < o->wasmEnd)
    {                                                                   emit_stack_dump (o);
        u8 opcode = * (o->wasm++);                                      log_opcode (o, opcode);
        const M3OpInfo * op = & c_operations [opcode];

        M3Compiler compiler = op->compiler;

        if (not compiler)
            compiler = Compile_Operator;

        result = (* compiler) (o, opcode);

        o->previousOpcode = opcode;                             //                      m3logif (stack, dump_type_stack (o))

        if (o->stackIndex > d_m3MaxFunctionStackHeight)         // TODO: is this only place to check?
            result = m3Err_functionStackOverflow;

        if (result)
            break;

        if (opcode == c_waOp_end or opcode == c_waOp_else)
            break;
    }

    return result;
}


M3Result  ValidateBlockEnd  (IM3Compilation o, bool * o_copyStackTopToRegister)
{
    M3Result result = m3Err_none;

    * o_copyStackTopToRegister = false;

    if (o->block.type != c_m3Type_none)
    {
        if (IsStackPolymorphic (o))
        {
_           (UnwindBlockStack (o));
_           (PushRegister (o, o->block.type));
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


M3Result  CompileBlock  (IM3Compilation o, /*pc_t * o_startPC,*/ u8 i_blockType, u8 i_blockOpcode)
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
    u32 numConstants = 0;

    bytes_t wa = o->wasm;
    while (wa < o->wasmEnd)
    {
        u8 code = * wa++;

        if (code >= 0x41 and code <= 0x44)
            ++numConstants;
    }                                                                                           m3log (compile, "estimated constants: %d", numConstants)

    o->firstSlotIndex = o->firstConstSlotIndex = o->constSlotIndex = o->stackIndex;

    // if constants overflow their reserved stack space, the compiler simply emits op_Const
    // operations as needed. Compiled expressions (global inits) don't pass through this
    // ReserveConstants function and thus always produce inline contants.
    numConstants = min (numConstants, d_m3MaxNumFunctionConstants);

    u32 freeSlots = d_m3MaxFunctionStackHeight - o->constSlotIndex;

    if (numConstants <= freeSlots)
        o->firstSlotIndex += numConstants;
    else
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
    M3Result result = m3Err_none;                                     m3log (compile, "compiling: '%s'; wasm-size: %d; numArgs: %d; return: %s",
                                                                           io_function->name, (u32) (io_function->wasmEnd - io_function->wasm), GetFunctionNumArgs (io_function), c_waTypes [GetFunctionReturnType (io_function)]);
    IM3Runtime runtime = io_function->module->runtime;

    IM3Compilation o = & runtime->compilation;
    SetupCompilation (o);

    o->runtime = runtime;
    o->module =  io_function->module;
    o->wasm =    io_function->wasm;
    o->wasmEnd = io_function->wasmEnd;
    o->page =    AcquireCodePage (runtime);

    if (o->page)
    {
        pc_t pc = GetPagePC (o->page);

        o->block.type = GetFunctionReturnType (io_function);
        o->function = io_function;

        // push the arg types to the type stack
        M3FuncType * ft = io_function->funcType;

        for (u32 i = 0; i < GetFunctionNumArgs (io_function); ++i)
        {
            u8 type = ft->argTypes [i];
_           (PushAllocatedSlot (o, type));
        }

_       (CompileLocals (o));

_       (Compile_ReserveConstants (o));

        // start tracking the max stack used (Push() also updates this value) so that op_Entry can precisely detect stack overflow
        o->function->maxStackSlots = o->firstSlotIndex;

        o->numAllocatedExecSlots = 0;               // this var only tracks dynamic slots so clear local+constant allocations
        o->block.initStackIndex = o->stackIndex;

_       (EmitOp (o, op_Entry));
        EmitPointer (o, io_function);

_       (Compile_BlockStatements (o));

        io_function->compiled = pc;

        u32 numConstants = o->constSlotIndex - o->firstConstSlotIndex;

        io_function->numConstants = numConstants;                   m3log (compile, "unique constants: %d; unused slots: %d", numConstants, o->firstSlotIndex - o->constSlotIndex);

        if (numConstants)
        {
_           (m3Alloc (& io_function->constants, u64, numConstants));

            memcpy (io_function->constants, o->constants, sizeof (u64) * numConstants);
        }
    }
    else _throw (m3Err_mallocFailedCodePage);

    _catch:

    ReleaseCompilationCodePage (o);

    return result;
}

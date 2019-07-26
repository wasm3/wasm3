//
//  m3_compile.c
//  M3: Massey Meta Machine
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include <assert.h>

#include "m3_compile.h"
#include "m3_emit.h"
#include "m3_exec.h"
#include "m3_exception.h"


//-------------------------------------------------------------------------------------------------------------------------

#define d_indent "     | "


const char *  GetOpcodeIndentionString  (IM3Compilation o)
{
	i32 blockDepth = o->block.depth + 1;
	
	if (blockDepth < 0)
		blockDepth = 0;
	
	static const char * s_spaces = ".......................................................................................";
	const char * indent = s_spaces + strlen (s_spaces);
	indent -= (blockDepth * 2);
	if (indent < s_spaces)
		indent = s_spaces;
	
	return indent;
}


const char *  GetIndentionString  (IM3Compilation o)
{
	o->block.depth += 4;
	const char *indent = GetOpcodeIndentionString (o);
	o->block.depth -= 4;
	
	return indent;
}


void emit_stack_dump (IM3Compilation o)
{
#	if d_m3RuntimeStackDumps
	if (o->numEmits)
	{
		EmitOp 			(o, op_DumpStack);
		EmitConstant	(o, o->numOpcodes);
		EmitConstant	(o, GetMaxExecSlot (o));
		EmitConstant	(o, (u64) o->function);
		
		o->numEmits = 0;
	}
# 	endif
}


void  log_opcode  (IM3Compilation o, u8 i_opcode)
{
	if (i_opcode == c_waOp_end or i_opcode == c_waOp_else)
		o->block.depth--;
	m3log (compile, "%4d | 0x%02x  %s %s", o->numOpcodes++, i_opcode, GetOpcodeIndentionString (o), c_operations [i_opcode].name);
	if (i_opcode == c_waOp_end or i_opcode == c_waOp_else)
		o->block.depth++;
}

//-------------------------------------------------------------------------------------------------------------------------

// just want less letter and numbers to stare at down the way in the compiler table
const u8 	i_32 	= c_m3Type_i32,
			i_64 	= c_m3Type_i64,
			f_32 	= c_m3Type_f32,
			f_64 	= c_m3Type_f64,
			none 	= c_m3Type_none,
			any		= -1
			;


bool  IsRegisterLocation		(i16 i_location)	{ return (i_location >= c_m3Reg0Id); }
bool  IsFpRegisterLocation		(i16 i_location)	{ return (i_location == c_m3Fp0Id);  }
bool  IsIntRegisterLocation		(i16 i_location)	{ return (i_location == c_m3Reg0Id); }


void  dump_type_stack  (IM3Compilation o)
{
#	if d_m3LogOutput
	printf ("                                                        ");
	for (u32 i = 0; i < o->stackIndex; ++i)
	{
		u16 s = o->wasmStack [i];
		
		if (IsRegisterLocation (s))
			printf ("%s", IsFpRegisterLocation (s) ? "fp0" : "r0");
		else
			printf ("%d", (i32) s);
		
		printf ("|%s ", c_waTypes [o->typeStack [i]]);
	}
# 	endif
}


i16  GetStackTopIndex  (IM3Compilation o)
{
	return o->stackIndex - 1;
}


u8  GetStackTopType  (IM3Compilation o)
{
	u8 type = c_m3Type_none;
	
	if (o->stackIndex)
		type = o->typeStack [o->stackIndex - 1];
	
	return type;
}


bool  IsStackTopTypeInt  (IM3Compilation o)
{
	return IsIntType (GetStackTopType (o));
}


bool  IsStackTopTypeFp  (IM3Compilation o)
{
	return IsFpType (GetStackTopType (o));
}


bool  IsStackTopInRegister  (IM3Compilation o)
{
	i16 i = GetStackTopIndex (o);				d_m3Assert (i >= 0);
	
	if (i >= 0)
	{
		return (o->wasmStack [i] >= c_m3Reg0Id);
	}
	else return false;
}


u16  GetStackTopExecSlot  (IM3Compilation o)
{
	i16 i = GetStackTopIndex (o);				d_m3Assert (i >= 0);
	
	u16 slot = 0;
	if (i >= 0)
		slot = o->wasmStack [i];
	
	return slot;
}


bool  IsStackTopMinus1InRegister  (IM3Compilation o)
{
	i16 i = GetStackTopIndex (o);
	
	if (i > 0)
	{
		return (o->wasmStack [i - 1] >= c_m3Reg0Id);
	}
	else return false;
}


void  MarkExecSlotAllocated  (IM3Compilation o, u16 i_slot)
{																	d_m3Assert (o->m3Slots [i_slot] == 0); // shouldn't be already allocated
	o->m3Slots [i_slot] = 1;
	o->numAllocatedExecSlots++;
}


bool  AllocateExecSlot  (IM3Compilation o, u16 * o_execSlot)
{
	bool found = false;
	
	// search for empty slot in the execution stack
	i16 i = o->firstSlotIndex;
	while (i < c_m3MaxFunctionStackHeight)
	{
		if (o->m3Slots [i] == 0)
		{
			MarkExecSlotAllocated (o, i);
			* o_execSlot = i;
			
			found = true;
			break;
		}
		
		++i;
	}
//	printf ("allocate %d\n", (i32) i);
	
	return found;
}


void DeallocateExecSlot (IM3Compilation o, i16 i_slotIndex)
{																						d_m3Assert (i_slotIndex >= o->firstSlotIndex);
	o->numAllocatedExecSlots--;															d_m3Assert (o->m3Slots [i_slotIndex]);
	o->m3Slots [i_slotIndex] = 0;
	
//	printf ("dealloc %d\n", (i32) i_stackIndex);
}


bool  IsRegisterAllocated  (IM3Compilation o, u32 i_register)
{
	return (o->regStackIndexPlusOne [i_register] != c_m3RegisterUnallocated);
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

	while (i < c_m3MaxFunctionStackHeight)
	{
		if (allocated == 0)
			break;
		
		if (o->m3Slots [i])
			--allocated;
		
		++i;
	}
	
	return i;
}


M3Result  PreserveRegisterIfOccupied  (IM3Compilation o, u8 i_type)
{
	M3Result result = c_m3Err_none;
	
	u32 regSelect = IsFpType (i_type);
	
	if (IsRegisterAllocated (o, regSelect))
	{
		u16 stackIndex = GetRegisterStackIndex (o, regSelect);
		DeallocateRegister (o, regSelect);
		
		// and point to a exec slot
		u16 slot;
		if (AllocateExecSlot (o, & slot))
		{
			o->wasmStack [stackIndex] = slot;
			
_			(EmitOp (o, regSelect ? op_SetSlot_f : op_SetSlot_i));
			EmitConstant (o, slot);
		}
		else throw (c_m3Err_functionStackOverflow);
	}
	
	catch: return result;
}


// all values must be in slots befor entering loop, if, and else blocks
// otherwise they'd end up preserve-copied in the block to probably different locations (if/else)
M3Result  PreserveRegisters  (IM3Compilation o)
{
	M3Result result;
	
_	(PreserveRegisterIfOccupied (o, c_m3Type_f64));
_	(PreserveRegisterIfOccupied (o, c_m3Type_i64));
	
	catch: return result;
}


M3Result  PreserveNonTopRegisters  (IM3Compilation o)
{
	M3Result result = c_m3Err_none;

	i16 stackTop = GetStackTopIndex (o);

	if (stackTop >= 0)
	{
		if (IsRegisterAllocated (o, 0))		// r0
		{
			if (GetRegisterStackIndex (o, 0) != stackTop)
_				(PreserveRegisterIfOccupied (o, c_m3Type_i64));
		}

		if (IsRegisterAllocated (o, 1))		// fp0
		{
			if (GetRegisterStackIndex (o, 1) != stackTop)
_				(PreserveRegisterIfOccupied (o, c_m3Type_f64));
		}
	}
	
	catch: return result;
}


//----------------------------------------------------------------------------------------------------------------------


void  Push  (IM3Compilation o, u8 i_m3Type, i16 i_location)
{
	u16 stackIndex = o->stackIndex++;										// printf ("push: %d\n", (i32) i);
	
	// wasmStack tracks read counts for args & locals. otherwise, wasmStack represents slot location.
	if (stackIndex < GetFunctionNumArgsAndLocals (o->function))
		i_location = 0;
	
	o->wasmStack 		[stackIndex] = i_location;
	o->typeStack 	[stackIndex] = i_m3Type;									m3logif (stack, dump_type_stack (o))

	if (IsRegisterLocation (i_location))
	{
		u32 regSelect = IsFpRegisterLocation (i_location);
		AllocateRegister (o, regSelect, stackIndex);
	}
}


void  PushRegister  (IM3Compilation o, u8 i_m3Type)
{
	i16 location = IsFpType (i_m3Type) ? c_m3Fp0Id : c_m3Reg0Id;
	Push (o, i_m3Type, location);
}


M3Result  Pop  (IM3Compilation o)
{
	M3Result result = c_m3Err_none;
	
	if (o->stackIndex)
	{
		o->stackIndex--;												//	printf ("pop: %d\n", (i32) o->stackIndex);

		i16 location = o->wasmStack [o->stackIndex];

		if (IsRegisterLocation (location))
		{
			u32 regSelect = IsFpRegisterLocation (location);
			DeallocateRegister (o, regSelect);
		}
		else if (location >= o->firstSlotIndex)
		{
			DeallocateExecSlot (o, location);
		}
		
		m3logif (stack, dump_type_stack (o))
	}
	else result = c_m3Err_functionStackUnderrun;
	
	return result;
}


M3Result  _PushAllocatedSlotAndEmit  (IM3Compilation o, u8 i_m3Type, bool i_doEmit)
{
	M3Result result = c_m3Err_none;

	u16 slot;
	
	if (AllocateExecSlot (o, & slot))
	{
		i16 i = o->stackIndex;
		if (i < c_m3MaxFunctionStackHeight)
		{
			Push 			(o, i_m3Type, slot);
			
			if (i_doEmit)
				EmitConstant	(o, slot);
		}
		else result = c_m3Err_functionStackOverflow;
	}
	else result = c_m3Err_functionStackOverflow;
	
	return result;
}


M3Result  PushAllocatedSlotAndEmit  (IM3Compilation o, u8 i_m3Type)
{
	return _PushAllocatedSlotAndEmit (o, i_m3Type, true);
}


M3Result  PushAllocatedSlot  (IM3Compilation o, u8 i_m3Type)
{
	return _PushAllocatedSlotAndEmit (o, i_m3Type, false);
}


M3Result  PushConst  (IM3Compilation o, u64 i_word, u8 i_m3Type)
{
	M3Result result = c_m3Err_none;

	i16 location = -1;

	u32 numConstants = o->constSlotIndex - o->firstConstSlotIndex;
	
	for (u32 i = 0; i < numConstants; ++i)
	{
		if (o->constants [i] == i_word)
		{
			location = o->firstConstSlotIndex + i;
			Push (o, i_m3Type, location);				// stack check here??
			break;
		}
	}
	
	if (location < 0)
	{
		if (o->constSlotIndex < o->firstSlotIndex)
		{
			o->constants [numConstants] = i_word;
			location = o->constSlotIndex++;
			
			Push (o, i_m3Type, location);		// stack check here??
		}
		else
		{
_			(EmitOp (o, op_Const));
			EmitConstant (o, i_word);
_			(PushAllocatedSlotAndEmit (o, i_m3Type));
		}
	}
	
	catch: return result;
}


M3Result  EmitTopSlotAndPop  (IM3Compilation o)
{
	if (not IsStackTopInRegister (o))
		EmitConstant (o, GetStackTopExecSlot (o));
	
	return Pop (o);
}


//-------------------------------------------------------------------------------------------------------------------------


M3Result CopyTopSlot (IM3Compilation o, u16 i_destSlot)
{
	M3Result result = c_m3Err_none;
	
	IM3Operation op;
	
	if (IsStackTopInRegister (o))
	{
		bool isFp = IsStackTopTypeFp (o);
		op = isFp ? op_SetSlot_f : op_SetSlot_i;
	}
	else op = op_CopySlot;
	
_	(EmitOp (o, op));
	EmitConstant (o, i_destSlot);
		
	if (not IsStackTopInRegister (o))
		EmitConstant (o, GetStackTopExecSlot (o));
	
	catch: return result;
}


// a copy-on-write strategy is used with locals. when a get local occurs, it's not copied anywhere. the stack
// entry just has a index pointer to that local memory slot.
// then, when a previously referenced local is set, the current value needs to be preserved for those references

M3Result  PreservedCopyTopSlot  (IM3Compilation o, u16 i_destSlot, u16 i_preserveSlot)
{
	M3Result result = c_m3Err_none;				d_m3Assert (i_destSlot != i_preserveSlot);
	
	IM3Operation op;
	
	if (IsStackTopInRegister (o))
	{
		bool isFp = IsStackTopTypeFp (o);
		op = isFp ? op_PreserveSetSlot_f : op_PreserveSetSlot_i;
	}
	else op = op_PreserveCopySlot;
	
_	(EmitOp (o, op));
	EmitConstant (o, i_destSlot);
		
	if (not IsStackTopInRegister (o))
		EmitConstant (o, GetStackTopExecSlot (o));
		
	EmitConstant (o, i_preserveSlot);
	
	catch: return result;
}



M3Result  MoveStackTopToRegister  (IM3Compilation o)
{
	M3Result result = c_m3Err_none;
	
	if (not IsStackTopInRegister (o))
	{
		u8 type = GetStackTopType (o);
		
//		if (
		
		IM3Operation op = IsFpType (type) ? op_SetRegister_f : op_SetRegister_i;
		
_		(EmitOp (o, op));
_		(EmitTopSlotAndPop (o));
		PushRegister (o, type);
	}
	
	catch: return result;
}



M3Result  ReturnStackTop  (IM3Compilation o)
{
	M3Result result = c_m3Err_none;

	i16 top = GetStackTopIndex (o);
	
	if (top >= 0)
	{
		const u16 returnSlot = 0;
		
		if (o->wasmStack [top] != returnSlot)
			CopyTopSlot (o, returnSlot);
	}
	else result = "stack underflow";
	
	return result;
}



// if local is unreferenced, o_preservedStackIndex will be equal to localIndex on return
M3Result  IsLocalReferencedWithCurrentBlock  (IM3Compilation o, u16 * o_preservedStackIndex, u32 i_localIndex)
{
	M3Result result = c_m3Err_none;
	
//	printf ("IsLocalReferenced: %d --> %d\n", o->block.initStackIndex, o->stackIndex);
	
	* o_preservedStackIndex = (u16) i_localIndex;
	
	for (u32 i = o->block.initStackIndex; i < o->stackIndex; ++i)
	{
		if (o->wasmStack [i] == i_localIndex)
		{
			if (* o_preservedStackIndex == i_localIndex)
			{
				if (not AllocateExecSlot (o, o_preservedStackIndex))
					throw (c_m3Err_functionStackOverflow);
			}
			
			o->wasmStack [i] = * o_preservedStackIndex;
		}
	}
	
	catch: return result;
}



bool  WasLocalModified  (u8 * i_bitmask, u32 i_localIndex)
{
	u32 byte = i_localIndex >> 3;
	u32 bit = i_localIndex & 0x7;
	
	return i_bitmask [byte] & (1 << bit);
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
	
	return c_m3Err_none;
}


//-------------------------------------------------------------------------------------------------------------------------


M3Result  Compile_Const_i32  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	i32 value;
_	(ReadLEB_i32 (& value, & o->wasm, o->wasmEnd));				m3log (compile, d_indent "%s (const i32 = %d)", GetIndentionString (o), value);
	
_	(PushConst (o, value, c_m3Type_i32));
	
	catch: return result;
}


M3Result  Compile_Const_i64  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	i64 value;
_	(ReadLEB_i64 (& value, & o->wasm, o->wasmEnd));				m3log (compile, d_indent "%s (const i32 = %lld)", GetIndentionString (o), value);
	
_	(PushConst (o, value, c_m3Type_i64));

	catch: return result;
}


M3Result  Compile_Const_f32  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	u32 value;
_	(ReadLEB_u32 (& value, & o->wasm, o->wasmEnd));				m3log (compile, d_indent "%s (const f32 = %f)", GetIndentionString (o), * ((f32 *) & value));
	
	// convert to double
	f64 f = * (f32 *) & value;
_	(PushConst (o, * (u64 *) & f, c_m3Type_f64));
	
	catch: return result;
}


M3Result  Compile_Const_f64  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	u64 value;
_	(Read_u64 (& value, & o->wasm, o->wasmEnd));				m3log (compile, d_indent "%s (const f64 = %lf)", GetIndentionString (o), * ((f64 *) & value));
	
_	(PushConst (o, value, c_m3Type_f64));

	catch: return result;
}


M3Result  Compile_Return  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	bool hasReturn = GetFunctionNumReturns (o->function);

	if (hasReturn)
	{
_		(ReturnStackTop (o));
_		(Pop (o));
	}
	
_	(EmitOp (o, op_Return));
	
	catch: return result;
}


// todo: else maps to nop now.
M3Result  Compile_Else_End  (IM3Compilation o, u8 i_opcode)
{
	M3Result result = c_m3Err_none;
	
	if (o->block.depth == 0)
	{
		if (o->block.type)
_			(ReturnStackTop (o));
	
_		(EmitOp (o, op_End));
	}
	
	catch: return result;
}



M3Result  Compile_SetLocal  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	u32 localSlot;
_	(ReadLEB_u32 (& localSlot, & o->wasm, o->wasmEnd));				//	printf ("--- set local: %d \n", localSlot);
	
	if (localSlot < GetFunctionNumArgsAndLocals (o->function))
	{
		u16 preserveSlot;
_		(IsLocalReferencedWithCurrentBlock (o, & preserveSlot, localSlot));  // preserve will be different than local, if referenced

		// increment modify count. modification count is just debug info
		#if DEBUG
			o->wasmStack [localSlot] ++;
			if (o->wasmStack [localSlot] == 0)
				o->wasmStack [localSlot] --;
		#else
			// but a modication flag is not
			o->wasmStack [localSlot] |= 0x8000;
		#endif
		
		if (preserveSlot == localSlot)
_			(CopyTopSlot (o, localSlot))
		else
_			(PreservedCopyTopSlot (o, localSlot, preserveSlot))

		if (i_opcode != c_waOp_teeLocal)
_			(Pop (o));
	}
	else throw ("local index out of bounds");
	
	catch: return result;
}


M3Result  Compile_GetLocal  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	u32 localIndex;
_	(ReadLEB_u32 (& localIndex, & o->wasm, o->wasmEnd));
	
	u8 type = o->typeStack [localIndex];
	Push (o, type, localIndex);
	
	catch: return result;
}


M3Result  Compile_GetGlobal  (IM3Compilation o, M3Global * i_global)
{
	M3Result result;

_	(EmitOp (o, op_GetGlobal));
	EmitPointer (o, & i_global->intValue);
_	(PushAllocatedSlotAndEmit (o, i_global->type));

	catch: return result;
}


M3Result  Compile_SetGlobal  (IM3Compilation o, M3Global * i_global)
{
	M3Result result = c_m3Err_none;

	IM3Operation op;
	
	if (IsStackTopInRegister (o))
		op = IsStackTopTypeFp (o) ? op_SetGlobal_f : op_SetGlobal_i;
	else
		op = op_SetGlobal_s;
	
_	(EmitOp (o, op));
	EmitPointer	(o, & i_global->intValue);
	
	if (op == op_SetGlobal_s)
		EmitConstant (o, GetStackTopExecSlot (o));

_	(Pop (o));

	catch: return result;
}


M3Result  Compile_GetSetGlobal  (IM3Compilation o, u8 i_opcode)
{
	M3Result result = c_m3Err_none;

	u32 globalIndex;
_	(ReadLEB_u32 (& globalIndex, & o->wasm, o->wasmEnd));

	if (globalIndex < o->module->numGlobals)
	{
		if (o->module->globals)
		{
			M3Global * global = & o->module->globals [globalIndex];
			
			result = (i_opcode == 0x23) ? Compile_GetGlobal (o, global) : Compile_SetGlobal (o, global);
		}
		else result = ErrorCompile (c_m3Err_globalMemoryNotAllocated, o, "module '%s' is missing global memory", o->module->name);
	}
	else result = ErrorCompile (c_m3Err_globaIndexOutOfBounds, o, "");
	
	catch: return result;
}


M3Result  Compile_Branch  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;

	u32 depth;
_	(ReadLEB_u32 (& depth, & o->wasm, o->wasmEnd));
	
//		printf ("depth: %d \n", depth);
	IM3CompilationScope scope;
_	(GetBlockScope (o, & scope, depth));
	
	IM3Operation op;
	
	if (scope->opcode == c_waOp_loop)
	{
		if (i_opcode == c_waOp_branchIf)
		{
_			(MoveStackTopToRegister (o));
			op = op_ContinueLoopIf;
_			(Pop (o));
		}
		else op = op_ContinueLoop;
		
_		(PreserveRegisters (o));

_		(EmitOp (o, op));
		EmitPointer (o, scope->pc);
	}
	else
	{
		if (i_opcode == c_waOp_branchIf)
		{
_			(MoveStackTopToRegister (o));
			op = op_BranchIf;
_			(Pop (o));
		}
		else op = op_Branch;

_		(PreserveRegisters (o));

_		(EmitOp (o, op));
		
		IM3BranchPatch patch = scope->patches;

_		(m3Alloc (& scope->patches, M3BranchPatch, 1));
		
//					printf ("scope: %p -- attach patch: %p to %p \n", scope, scope->patches, patch);
		scope->patches->location = ReservePointer (o);
		scope->patches->next = patch;
	}
	
	catch: return result;
}


M3Result  Compile_BranchTable  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	u32 targetCount;
_	(ReadLEB_u32 (& targetCount, & o->wasm, o->wasmEnd));
	
	u32 numCodeLines = targetCount + 3;	// 3 => IM3Operation + target_count + default_target
	
_	(EnsureCodePageNumLines (o, numCodeLines));
	
_	(MoveStackTopToRegister (o));
_	(Pop (o));

_	(PreserveRegisters (o));

_	(EmitOp (o, op_BranchTable));
	EmitConstant (o, targetCount);
	
	++targetCount; // include default
	for (u32 i = 0; i < targetCount; ++i)
	{
		u32 target;
_		(ReadLEB_u32 (& target, & o->wasm, o->wasmEnd));
		
		IM3CompilationScope scope;
_		(GetBlockScope (o, & scope, target));

		if (scope->opcode == c_waOp_loop)
		{
			printf ("not implemented\n");
			abort ();
		}
		else
		{
			IM3BranchPatch patch = scope->patches;
_			(m3Alloc (& scope->patches, M3BranchPatch, 1));
			
			scope->patches->location = ReservePointer (o);
			scope->patches->next = patch;
		}
	}

	catch: return result;
}


M3Result  CompileCallArgsReturn  (IM3Compilation o, IM3FuncType i_type, bool i_isIndirect)
{
	M3Result result = c_m3Err_none;

	u16 execTop = GetMaxExecSlot (o);
	u32 numArgs = i_type->numArgs + i_isIndirect;
	u16 argTop = execTop + numArgs;
	
	while (numArgs--)
	{
_		(CopyTopSlot (o, --argTop));
_		(Pop (o));
	}
	
	i32 numReturns = i_type->returnType ? 1 : 0;
	
	if (numReturns)
	{
		o->m3Slots [execTop] = 1;
		o->numAllocatedExecSlots++;
		
		Push (o, i_type->returnType, execTop);
	}

	catch: return result;
}


M3Result  Compile_Call  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;

	u32 functionIndex;
_	(ReadLEB_u32 (& functionIndex, & o->wasm, o->wasmEnd));

	IM3Function function = Module_GetFunction (o->module, functionIndex);
	
	if (function)
	{																	m3log (compile, d_indent "%s (func= '%s'; args= %d)",
																				GetIndentionString (o), GetFunctionName (function), function->funcType->numArgs);
		if (function->module)
		{
			// OPTZ: could avoid arg copy when args are already sequential and at top
			
			u16 execTop = GetMaxExecSlot (o);

_			(CompileCallArgsReturn (o, function->funcType, false));
			
			IM3Operation op;
			const void * operand;
			
			if (function->compiled)
			{
				op = op_Call;
				operand = function->compiled;
			}
			else
			{															d_m3Assert (function->module);	// not linked
				op = op_Compile;
				operand = function;
			}

_			(EmitOp		(o, op));
			EmitPointer	(o, operand);
			EmitOffset	(o, execTop);
		}
		else result = ErrorCompile (c_m3Err_functionImportMissing, o, "missing function: '%s'", GetFunctionName (function));
	}
	else result = c_m3Err_functionLookupFailed;
	
	catch: return result;
}


M3Result  Compile_CallIndirect  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;

	u32 typeIndex;
_	(ReadLEB_u32 (& typeIndex, & o->wasm, o->wasmEnd));

	i8 reserved;
_	(ReadLEB_i7 (& reserved, & o->wasm, o->wasmEnd));

	if (typeIndex < o->module->numFuncTypes)
	{
		u16 execTop = GetMaxExecSlot (o);
		
		IM3FuncType type = & o->module->funcTypes [typeIndex];
_		(CompileCallArgsReturn (o, type, true));
		
_		(EmitOp		(o, op_CallIndirect));
		EmitPointer	(o, o->module);
		EmitPointer	(o, type);				// TODO: unify all types in M3Rsuntime
		EmitOffset	(o, execTop);
	}
	else throw ("function type index out of range");
	
	catch: return result;
}


M3Result  ReadBlockType  (IM3Compilation o, u8 * o_blockType)
{
	M3Result result;														d_m3Assert (o_blockType);

	i8 type;
	
	_	(ReadLEB_i7 (& type, & o->wasm, o->wasmEnd));
	_	(NormalizeType (o_blockType, type));								if (* o_blockType)	m3log (compile, d_indent "%s (block_type: 0x%02x normalized: %d)",
																								   GetIndentionString (o), (u32) (u8) type, (u32) * o_blockType);
	catch: return result;
}


M3Result  Compile_LoopOrBlock  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
_ 	(PreserveRegisters (o));
	
	u8 blockType;
_	(ReadBlockType (o, & blockType));

_	(EmitOp (o, i_opcode == 0x03 ? op_Loop : op_Block));			// TODO: block operation not required

_	(Compile_Block (o, blockType, i_opcode));

	catch: return result;
}


M3Result  Compile_If  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;

_	(PreserveNonTopRegisters (o));
	
	IM3Operation op = IsStackTopInRegister (o) ? op_If_r : op_If_s;

_	(EmitOp (o, op));
_	(EmitTopSlotAndPop (o));

	i32 stackIndex = o->stackIndex;

	pc_t * preservations = ReservePointer (o);
	pc_t * pc = ReservePointer (o);

	u8 blockType;
_	(ReadBlockType (o, & blockType));

_	(Compile_Block (o, blockType, i_opcode));
	
	if (o->previousOpcode == c_waOp_else)
	{
		if (blockType and o->stackIndex > stackIndex)
		{
_			(Pop (o));
		}
		
_		(Compile_ElseBlock (o, pc, blockType));
	}
	else * pc = GetPC (o);
	
	catch: return result;
}


M3Result  Compile_Select  (IM3Compilation o, u8 i_opcode)
{
	M3Result result = c_m3Err_none;

	IM3Operation ops [] = { op_Select_i_ssr, op_Select_i_srs, op_Select_i_rss };
	
	u16 slots [3] = { 0xffff, 0xffff, 0xffff };
	
	IM3Operation op = op_Select_i_sss;
	
	u8 type = 0;
	for (u32 i = 0; i < 3; i++)
	{
		if (IsStackTopInRegister (o))
			op = ops [i];
		else
			slots [i] = GetStackTopExecSlot (o);
		
		type = GetStackTopType (o);
_		(Pop (o));
	}
	
	assert (IsIntType (type));		// fp unimplemented

	// this operation doesn't consume a register, so might have to protected its contents
	if (op == op_Select_i_sss)
_		(PreserveRegisterIfOccupied (o, type));

_	(EmitOp (o, op));
	
	for (u32 i = 0; i < 3; i++)
	{
		if (slots [i] != 0xffff)
			EmitConstant (o, slots [i]);
	}
	
	PushRegister (o, type);

	catch: return result;
}


M3Result  Compile_Drop  (IM3Compilation o, u8 i_opcode)
{
	return Pop (o);
}


M3Result  Compile_Nop  (IM3Compilation o, u8 i_opcode)
{
	return c_m3Err_none;
}


M3Result  Compile_Trap  (IM3Compilation o, u8 i_opcode)
{
	return EmitOp (o, op_Trap);
}


// OPTZ: currently all stack slot indicies take up a full word, but
// dual stack source operands could be packed together
M3Result  Compile_Operator  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	const M3OpInfo * op = & c_operations [i_opcode];

	IM3Operation operation;
	
	if (op->stackOffset == 0)
	{
		if (IsStackTopInRegister (o))
		{
			operation = op->operation_sr;
		}
		else
		{
_			(PreserveRegisterIfOccupied (o, op->type));
			operation = op->operation_rs;
		}
	}
	else
	{
		if (IsStackTopInRegister (o))
		{
			operation = op->operation_sr;					// printf ("r <- s+r\n");
		}
		else if (IsStackTopMinus1InRegister (o))
		{
			operation = op->operation_rs;
			
			if (not operation)	// must be commutative, then
				operation = op->operation_sr;
															// printf ("r <- r+s\n");
		}
		else
		{
_			(PreserveRegisterIfOccupied (o, op->type));
			operation = op->operation_ss;					// printf ("r <- s+s\n");
		}
	}
	
	if (operation)
	{
_		(EmitOp (o, operation));
		
//				if (op->type != c_m3Type_none)
		{
_			(EmitTopSlotAndPop (o));
			
			if (op->stackOffset < 0)
_				(EmitTopSlotAndPop (o));
			
			if (op->type != c_m3Type_none)
				PushRegister (o, op->type);
		}
	}
	else
	{
		result = "fail";	assert (false);
	}
	
	catch: return result;
}


M3Result  Compile_Load_Store  (IM3Compilation o, u8 i_opcode)
{
	M3Result result;
	
	u32 alignHint, offset;

_	(ReadLEB_u32 (& alignHint, & o->wasm, o->wasmEnd));
_	(ReadLEB_u32 (& offset, & o->wasm, o->wasmEnd));
																		m3log (compile, d_indent "%s (offset = %d)", GetIndentionString (o), offset);
_	(Compile_Operator (o, i_opcode));
	EmitConstant (o, offset);
	
	catch: return result;
}


#define d_unaryOpList(TYPE, NAME) op_##TYPE##_##NAME##_r, op_##TYPE##_##NAME##_s, NULL
#define d_binOpList(TYPE, NAME) op_##TYPE##_##NAME##_sr, op_##TYPE##_##NAME##_rs, op_##TYPE##_##NAME##_ss
#define d_commutativeBinOpList(TYPE, NAME) op_##TYPE##_##NAME##_sr, NULL, op_##TYPE##_##NAME##_ss


const M3OpInfo c_operations [] =
{
	{ "unreachable",		 0, none,	NULL,	NULL, NULL, 					Compile_Trap },			// 0x00
	{ "nop",				 0, none, 	NULL,	NULL, NULL,						Compile_Nop },			// 0x01 .
	{ "block",				 0, none,	NULL,	NULL, NULL,	 					Compile_LoopOrBlock },	// 0x02
	{ "loop",				 0,	none,	NULL,	NULL, NULL,						Compile_LoopOrBlock },	// 0x03
	{ "if",					-1,	none,	NULL,	NULL, NULL,						Compile_If },			// 0x04
	{ "else",				 0, none,	NULL,	NULL, NULL,						Compile_Else_End },		// 0x05
	
	{ "reserved" }, { "reserved" }, { "reserved" }, { "reserved" }, { "reserved" },						// 0x06 - 0x0a
	
	{ "end",				 0,	none,	NULL,	NULL,	NULL,					Compile_Else_End },		// 0x0b
	{ "br",					 0,	none,	NULL,	NULL,	NULL,					Compile_Branch },		// 0x0c
	{ "br_if",				-1,	none,	NULL,	NULL,	NULL,					Compile_Branch },		// 0x0d
	{ "br_table",			-1,	none, 	NULL,	NULL,	NULL,					Compile_BranchTable },	// 0x0e
	{ "return",				 0,	any,	NULL,	NULL,	NULL,					Compile_Return },		// 0x0f
	{ "call",				 0,	any,	NULL,	NULL,	NULL,					Compile_Call },			// 0x10
	{ "call_indirect",		 0,	any,	NULL,	NULL,	NULL,					Compile_CallIndirect },	// 0x11
	
	{ "reserved" },	{ "reserved" },	{ "reserved" },	{ "reserved" },										// 0x12 - 0x15
	{ "reserved" },	{ "reserved" }, { "reserved" }, { "reserved" },										// 0x16 - 0x19
	
	{ "drop",				-1,	none,	NULL,	NULL,	NULL,					Compile_Drop },			// 0x1a
	{ "select",				-2,	any,	NULL,	NULL,	NULL,					Compile_Select	},		// 0x1b
	
	{ "reserved" },	{ "reserved" }, { "reserved" }, { "reserved" },										// 0x1c - 0x1f
	
	{ "local.get",			1,	any,	NULL,	NULL,	NULL,					Compile_GetLocal },		// 0x20
	{ "local.set",			1,	none,	NULL,	NULL,	NULL,					Compile_SetLocal },		// 0x21
	{ "local.tee",			0,	any,	NULL,	NULL,	NULL,					Compile_SetLocal},		// 0x22
	{ "global.get",			1,	none,	NULL,	NULL,	NULL,					Compile_GetSetGlobal },	// 0x23
	{ "global.set",			1,	none,	NULL,	NULL,	NULL,					Compile_GetSetGlobal },	// 0x24
	
	{ "reserved" },	{ "reserved" }, { "reserved" }, 													// 0x25 - 0x27
	
	{ "i32.load",			0,	i_32,	op_i32_Load_i32_r, op_i32_Load_i32_s, NULL,			Compile_Load_Store },			// 0x28
	{ "i64.load",			0,	i_64, 	NULL,	 			NULL, NULL				},								// 0x29
	{ "f32.load",			0,	f_32,	NULL, 			NULL, NULL},								// 0x2a
	{ "f64.load",			0,	f_64,	NULL, 			NULL, NULL},								// 0x2b
	
	{ "i32.load8_s",		0,	i_32,	op_i32_Load_i8_r, 		op_i32_Load_i8_s, NULL,				Compile_Load_Store },			// 0x2c
	{ "i32.load8_u",		0,	i_32,	op_i32_Load_u8_r, 		op_i32_Load_u8_s, NULL,				Compile_Load_Store },			// 0x2d
	{ "i32.load16_s",		0,	i_32,	op_i32_Load_i16_r,		op_i32_Load_i16_s, NULL,			Compile_Load_Store },			// 0x2e
	{ "i32.load16_u",		0,	i_32,	op_i32_Load_u16_r, 		op_i32_Load_u16_s,  NULL,			Compile_Load_Store },			// 0x2f
	
	{ "i64.load8_s",		0,	i_64,	NULL, 			NULL, NULL,			},			// 0x30
	{ "i64.load8_u",		0,	i_64,	NULL,		NULL, NULL },			// 0x31
	{ "i64.load16_s",		0,	i_64,	NULL, 		NULL, NULL },			// 0x32
	{ "i64.load16_u",		0,	i_64,	NULL, 		NULL, NULL},			// 0x33
	{ "i64.load32_s",		0,	i_64,	NULL, 		NULL, NULL },			// 0x34
	{ "i64.load32_u",		0,	i_64,	NULL, 		NULL, NULL },			// 0x35
	
	{ "i32.store",			-2,	none,	d_binOpList (i32, Store_i32),				Compile_Load_Store },		// 0x36
	{ "i64.store",			-2,	none,	NULL, 		NULL, NULL },		// 0x37
	{ "f32.store",			-2,	none,	NULL, 			NULL, NULL },		// 0x38
	{ "f64.store",			-2,	none,	op_f64_Store, 		NULL, NULL,	 			Compile_Load_Store },		// 0x39
	
	{ "i32.store8",			-2,	none,	d_binOpList (i32, Store_u8),  				Compile_Load_Store },		// 0x3a
	{ "i32.store16",		-2,	none,	d_binOpList (i32, Store_i16),  				Compile_Load_Store },		// 0x3b
	
	{ "i64.store8",			-2,	none,	NULL, 				NULL, NULL  },		// 0x3c
	{ "i64.store16",		-2,	none,	NULL, 			NULL, NULL  },		// 0x3d
	{ "i64.store32",		-2,	none,	NULL, 			NULL, NULL  },		// 0x3e
	
	{ "current_memory",		1,	i_64,	NULL, 			NULL, NULL  },		// 0x3f		// FIX!! don't know about these stck offs
	{ "grow_memory",		-1,	i_64,	NULL, 			NULL, NULL  },		// 0x40
	
	{ "i32.const",			1,	i_32,	NULL, 			NULL, NULL,						Compile_Const_i32 },			// 0x41
	{ "i64.const",			1,	i_64,	NULL,		NULL, NULL,		Compile_Const_i64 },			// 0x42
	{ "f32.const",			1,	f_32,	NULL, 					NULL, NULL,					Compile_Const_f32 },		// 0x43
	{ "f64.const",			1,	f_64,	NULL, 				NULL, NULL,						Compile_Const_f64 },			// 0x44
	
	{ "i32.eqz",			0,	i_32,	d_unaryOpList (i32, EqualToZero)		},			// 0x45
	{ "i32.eq",				-1,	i_32,	d_binOpList (i32, Equal) 				},			// 0x46
	{ "i32.ne",				-1,	i_32,	d_binOpList (i32, NotEqual) 			},			// 0x47
	{ "i32.lt_s",			-1,	i_32,	d_binOpList (i32, LessThan) 			},			// 0x48
	{ "i32.lt_u",			-1,	i_32,	d_binOpList (u32, LessThan) 			},			// 0x49
	{ "i32.gt_s",			-1,	i_32,	d_binOpList (i32, GreaterThan)			},			// 0x4a
	{ "i32.gt_u",			-1,	i_32,	d_binOpList (u32, GreaterThan)			},			// 0x4b
	{ "i32.le_s",			-1,	i_32,	d_binOpList (i32, LessThanOrEqual)		},			// 0x4c
	{ "i32.le_u",			-1,	i_32,	d_binOpList (u32, LessThanOrEqual)		},			// 0x4d
	{ "i32.ge_s",			-1,	i_32,	d_binOpList	(i32, GreaterThanOrEqual)	},			// 0x4e
	{ "i32.ge_u",			-1,	i_32,	d_binOpList	(u32, GreaterThanOrEqual)	},			// 0x4f
	
	{ "i64.eqz",			0,	i_32,	d_unaryOpList (i64, EqualToZero)		},			// 0x50
	{ "i64.eq",				-1,	i_32,	d_binOpList (i64, Equal)				},			// 0x51
	{ "i64.ne",				-1,	i_32,	d_binOpList (i64, NotEqual)				},			// 0x52
	{ "i64.lt_s",			-1,	i_32,	d_binOpList	(i64, LessThan)				},			// 0x53
	{ "i64.lt_u",			-1,	i_32,	d_binOpList	(u64, LessThan)				},			// 0x54
	{ "i64.gt_s",			-1,	i_32,	d_binOpList	(i64, GreaterThan)			},			// 0x55
	{ "i64.gt_u",			-1,	i_32,	d_binOpList	(u64, GreaterThan)			},			// 0x56
	{ "i64.le_s",			-1,	i_32,	d_binOpList	(i64, LessThanOrEqual)		},			// 0x57
	{ "i64.le_u",			-1,	i_32,	d_binOpList	(u64, LessThanOrEqual)		},			// 0x58
	{ "i64.ge_s",			-1,	i_32,	d_binOpList	(i64, GreaterThanOrEqual)	},			// 0x59
	{ "i64.ge_u",			-1,	i_32,	d_binOpList	(u64, GreaterThanOrEqual)	},			// 0x5a
	
	{ "f32.eq",				-1,	i_32,	d_binOpList (f64, Equal)				},			// 0x5b
	{ "f32.ne",				-1,	i_32,	d_binOpList (f64, NotEqual) 			},			// 0x5c
	{ "f32.lt",				-1,	i_32,	d_binOpList (f64, LessThan)				},			// 0x5d
	{ "f32.gt",				-1,	i_32,	d_binOpList (f64, GreaterThan)			},			// 0x5e
	{ "f32.le",				-1,	i_32,	d_binOpList (f32, LessThanOrEqual)		},			// 0x5f
	{ "f32.ge",				-1,	i_32,	d_binOpList (f32, GreaterThanOrEqual)	},			// 0x60
	
	{ "f64.eq",				-1,	i_32,	d_binOpList (f64, Equal)				},			// 0x61
	{ "f64.ne",				-1,	i_32,	d_binOpList (f64, NotEqual) 			},			// 0x62
	{ "f64.lt",				-1,	i_32,	d_binOpList (f64, LessThan)				},			// 0x63
	{ "f64.gt",				-1,	i_32,	d_binOpList (f64, GreaterThan) 			},			// 0x64
	{ "f64.le",				-1,	i_32,	d_binOpList (f64, LessThanOrEqual) 		},			// 0x65
	{ "f64.ge",				-1,	i_32,	d_binOpList (f64, GreaterThanOrEqual)	},			// 0x66
	
	{ "i32.clz",			1,	i_32,	NULL, 			NULL, NULL },			// 0x67
	{ "i32.ctz",			1,	i_32,	NULL, 				NULL, NULL },			// 0x68
	{ "i32.popcnt",			1,	i_32,	NULL, 			NULL, NULL },			// 0x69
	
	{ "i32.add",			-1, i_32,	d_commutativeBinOpList (i32, Add)		},			// 0x6a
	{ "i32.sub",			-1, i_32,	d_binOpList	(i32, Subtract)				},			// 0x6b
	{ "i32.mul",			-1, i_32,	d_commutativeBinOpList (i32, Multiply)	},			// 0x6c
	{ "i32.div_s",			-1, i_32,	d_binOpList (i32, Divide) 				},			// 0x6d
	{ "i32.div_u",			-1, i_32,	d_binOpList (u32, Divide) 				},			// 0x6e
	{ "i32.rem_s",			-1, i_32,	d_binOpList (i32, Remainder)			},			// 0x6f
	{ "i32.rem_u",			-1, i_32,	d_binOpList (u32, Remainder) 			},			// 0x70
	{ "i32.and",			-1, i_32,	d_commutativeBinOpList (u64, And)		},			// 0x71
	{ "i32.or",				-1, i_32,	d_commutativeBinOpList (u64, Or)		},			// 0x72
	{ "i32.xor",			-1, i_32,	d_commutativeBinOpList (u64, Xor)		},			// 0x73
	{ "i32.shl",			-1, i_32,	d_binOpList (i32, ShiftLeft)			},			// 0x74
	{ "i32.shr_s",			-1, i_32,	d_binOpList (i32, ShiftRight)			},			// 0x75
	{ "i32.shr_u",			-1, i_32,	d_binOpList (u32, ShiftRight)			},			// 0x76
	{ "i32.rotl",			-1, i_32,	NULL, 			NULL, NULL },			// 0x77
	{ "i32.rotr",			-1, i_32,	NULL, 				NULL, NULL },			// 0x78
	
	{ "i64.clz",			1,	i_32,	NULL, 			NULL, NULL },			// 0x79
	{ "i64.ctz",			1,	i_32,	NULL, 			NULL, NULL },			// 0x7a
	{ "i64.popcnt",			1,	i_32,	NULL, 			NULL, NULL 	},			// 0x7b
	
	{ "i64.add",			-1,	i_64,	d_commutativeBinOpList (i64, Add)		},			// 0x7c
	{ "i64.sub",			-1,	i_64,	d_binOpList (i64, Subtract)				},			// 0x7d
	{ "i64.mul",			-1,	i_64,	NULL, 			NULL, NULL },			// 0x7e
	{ "i64.div_s",			-1,	i_64,	d_binOpList (i64, Divide) },			// 0x7f
	{ "i64.div_u",			-1,	i_64,	NULL, 			NULL, NULL },			// 0x80
	{ "i64.rem_s",			-1,	i_64,	NULL, 			NULL, NULL },			// 0x81
	{ "i64.rem_u",			-1,	i_64,	NULL, 			NULL, NULL },			// 0x82
	{ "i64.and",			-1,	i_64,	d_commutativeBinOpList (u64, And)		},			// 0x83
	{ "i64.or",				-1,	i_64,	d_commutativeBinOpList (u64, Or)		},			// 0x84
	{ "i64.xor",			-1,	i_64,	d_commutativeBinOpList (u64, Xor)		},			// 0x85
	{ "i64.shl",			-1,	i_64,	d_binOpList (i64, ShiftLeft)			},			// 0x86
	{ "i64.shr_s",			-1,	i_64,	d_binOpList (i64, ShiftRight)			},			// 0x87
	{ "i64.shr_u",			-1,	i_64,	d_binOpList (u64, ShiftRight)			},			// 0x88
	{ "i64.rotl",			-1,	i_64,	NULL, 		NULL, NULL },			// 0x89
	{ "i64.rotr",			-1,	i_64,	NULL, 			NULL, NULL },			// 0x8a
	
	{ "f32.abs",			0,	f_32,	op_f32_Abs, 		NULL, NULL },			// 0x8b
	{ "f32.neg",			0,	f_32,	NULL, 			NULL, NULL },			// 0x8c
	{ "f32.ceil",			0,	f_32,	op_f32_Ceil, 		NULL, NULL },			// 0x8d
	{ "f32.floor",			0,	f_32,	op_f32_Floor, 		NULL, NULL },			// 0x8e
	{ "f32.trunc",			0,	f_32,	op_f32_Trunc, 		NULL, NULL },			// 0x8f
	{ "f32.nearest",		0,	f_32,	NULL, 			NULL, NULL },			// 0x90
	{ "f32.sqrt",			0,	f_32,	op_f32_Sqrt, 		NULL, NULL },			// 0x91
	
	{ "f32.add",			-1,	f_32,	d_commutativeBinOpList (f32, Add) },				// 0x92
	{ "f32.sub",			-1,	f_32,	d_binOpList (f32, Subtract) },						// 0x93
	{ "f32.mul",			-1,	f_32,	d_commutativeBinOpList (f32, Multiply) },			// 0x94
	{ "f32.div",			-1,	f_32,	d_binOpList (f32, Divide) },						// 0x95
	{ "f32.min",			-1,	f_32,	op_f32_Min, 		NULL, NULL },					// 0x96
	{ "f32.max",			-1,	f_32,	op_f32_Max, 			NULL, NULL },				// 0x97
	{ "f32.copysign",		-1,	f_32,	op_f32_CopySign, 		NULL, NULL },				// 0x98
	
	{ "f64.abs",			0,	f_64,	op_f64_Abs, 		NULL, NULL },			// 0x99
	{ "f64.neg",			0,	f_64,	NULL, 				NULL, NULL },			// 0x9a
	{ "f64.ceil",			0,	f_64,	op_f64_Ceil, 		NULL, NULL },			// 0x9b
	{ "f64.floor",			0,	f_64,	op_f64_Floor, 		NULL, NULL },			// 0x9c
	{ "f64.trunc",			0,	f_64,	op_f64_Trunc, 		NULL, NULL },			// 0x9d
	{ "f64.nearest",		0,	f_64,	NULL, 				NULL, NULL },			// 0x9e
	{ "f64.sqrt",			0,	f_64,	op_f64_Sqrt, 		NULL, NULL },			// 0x9f
	
	{ "f64.add",			-1,	f_64, 	d_commutativeBinOpList (f64, Add)},					// 0xa0
	{ "f64.sub",			-1,	f_64, 	d_binOpList (f64, Subtract) },						// 0xa1
	{ "f64.mul",			-1,	f_64, 	d_commutativeBinOpList (f64, Multiply) },			// 0xa2
	{ "f64.div",			-1,	f_64, 	d_binOpList (f64, Divide) },						// 0xa3
	{ "f64.min",			-1,	f_64, 	op_f64_Min, 		NULL, NULL },			// 0xa4
	{ "f64.max",			-1,	f_64, 	op_f64_Max, 			NULL, NULL },			// 0xa5
	{ "f64.copysign",		-1,	f_64, 	op_f64_CopySign, 			NULL, NULL },			// 0xa6
	
	{ "i32.wrap/i64",		0,	i_32,	op_Nop, 		NULL, NULL 		},			// 0xa7
	{ "i32.trunc_s/f32",	0,	i_32,	NULL, 			NULL, NULL },			// 0xa8
	{ "i32.trunc_u/f32",	0,	i_32,	NULL, 			NULL, NULL },			// 0xa9
	{ "i32.trunc_s/f64",	0,	i_32,	op_i32_Truncate_f64,		NULL, NULL },			// 0xaa
	{ "i32.trunc_u/f64",	0,	i_32,	NULL, 				NULL, NULL  },			// 0xab
	
	{ "i64.extend_s/i32",	0,	i_64,	op_Extend_s, 		NULL, NULL },			// 0xac
	{ "i64.extend_u/i32",	0,	i_64,	op_Extend_u,		NULL, NULL  },			// 0xad
	{ "i64.trunc_s/f32",	0,	i_64,	NULL, 			NULL, NULL },			// 0xae
	{ "i64.trunc_u/f32",	0,	i_64,	NULL, 			NULL, NULL },			// 0xaf
	{ "i64.trunc_s/f64",	0,	i_64,	NULL, 		NULL, NULL },			// 0xb0
	{ "i64.trunc_u/f64",	0,	i_64,	NULL, 		NULL, NULL },			// 0xb1
	
	{ "f32.convert_s/i32",	0,	f_32, 	NULL, NULL, NULL },			// 0xb2
	{ "f32.convert_u/i32",	0,	f_32, 	NULL, 	NULL, NULL },			// 0xb3
	{ "f32.convert_s/i64",	0,	f_32, 	NULL, NULL, NULL },			// 0xb4
	{ "f32.convert_u/i64",	0,	f_32, 	NULL, 		NULL, NULL },			// 0xb5
	{ "f32.demote/f64",		0,	f_32, 	op_f32_Demote_r, 		op_f32_Demote_s, 	NULL },			// 0xb6
	
	{ "f64.convert_s/i32",	0,	f_64,	op_f64_Convert_i32_r,	op_f64_Convert_i32_s },			// 0xb7
	{ "f64.convert_u/i32",	0,	f_64,	NULL,NULL, NULL },			// 0xb8
	{ "f64.convert_s/i64",	0,	f_64,	NULL,NULL, NULL },			// 0xb9
	{ "f64.convert_u/i64",	0,	f_64,	NULL,NULL, NULL },			// 0xba
	{ "f64.promote/f32",	0,	f_64,	op_Nop,	NULL, NULL },			// 0xbb
	
	{ "i32.reinterpret/f32", 0,	i_32,	NULL,	NULL, NULL },			// 0xbc
	{ "i64.reinterpret/f64", 0,	i_64,	NULL,	NULL, NULL },			// 0xbd
	{ "f32.reinterpret/i32", 0,	f_32,	NULL,NULL, NULL },			// 0xbe
	{ "f64.reinterpret/i64", 0,	f_64,	NULL,NULL, NULL },			// 0xbf
	
	// for code logging
	{ "Const",				1,	any,	op_Const },
	{ NULL } 														// null termination for FindOperationInfo ()
};


M3Result  Compile_BlockStatements  (IM3Compilation o)
{
	M3Result result = c_m3Err_none;
	
	while (o->wasm < o->wasmEnd)
	{																	emit_stack_dump (o);
		u8 opcode = * (o->wasm++);										log_opcode (o, opcode);
		const M3OpInfo * op = & c_operations [opcode];
		
		M3Compiler compiler = op->compiler;

		if (not compiler)
			compiler = Compile_Operator;

		if (compiler)
			result = (* compiler) (o, opcode);
		else
			result = c_m3Err_noCompiler;
		
		o->previousOpcode = opcode;								//						m3logif (stack, dump_type_stack (o))
		
		if (o->stackIndex > c_m3MaxFunctionStackHeight)			// TODO: is this only place to check?
			result = c_m3Err_functionStackOverflow;
		
		if (result)
			break;
		
		if (opcode == c_waOp_end or opcode == c_waOp_else)
			break;
	}
	
	return result;
}


M3Result  ValidateBlockEnd  (IM3Compilation o, bool * o_copyStackTopToRegister)
{
	M3Result result = c_m3Err_none;
	
	* o_copyStackTopToRegister = false;
	
	i16 initStackIndex = o->block.initStackIndex;

	if (o->block.type != c_m3Type_none)
	{
		if (o->block.depth > 0 and initStackIndex != o->stackIndex)
		{
			if (o->stackIndex == initStackIndex + 1)
			{
				* o_copyStackTopToRegister = not IsStackTopInRegister (o);
			}
			else throw ("unexpected block stack offset");
		}
	}
	else
	{
		if (initStackIndex != o->stackIndex)
			m3log (compile, "reseting stack index");
		
		o->stackIndex = initStackIndex;
	}
	
	catch: d_m3Assert (not result);
	
	return result;
}


M3Result  Compile_BlockScoped  (IM3Compilation o, /*pc_t * o_startPC,*/ u8 i_blockType, u8 i_blockOpcode)
{
	M3Result result;

	M3CompilationScope outerScope = o->block;
	M3CompilationScope * block = & o->block;

	block->outer			= & outerScope;
	block->pc				= GetPagePC (o->page);
	block->patches			= NULL;
	block->type				= i_blockType;
	block->initStackIndex	= o->stackIndex;
	block->depth			++;
	block->loopDepth		+= (i_blockOpcode == c_waOp_loop);
	block->opcode			= i_blockOpcode;

_	(Compile_BlockStatements (o));
	
	while (block->patches)
	{														// printf ("%p patching: %p\n", block->patches, block->patches->location);
		IM3BranchPatch next = block->patches->next;
		
		pc_t * location = block->patches->location;
		* location = GetPC (o);
		
		free (block->patches);
		block->patches = next;
	}
	
	bool moveStackTopToRegister;
_	(ValidateBlockEnd (o, & moveStackTopToRegister));
	
	if (moveStackTopToRegister)
_		(MoveStackTopToRegister (o));

	o->block = outerScope;

	catch: return result;
}


M3Result  Compile_Block  (IM3Compilation o, u8 i_blockType, u8 i_blockOpcode)
{
	M3Result result;
	
	u32 numArgsAndLocals = GetFunctionNumArgsAndLocals (o->function);
	
	// save and clear the locals modification slots
	u16 locals [numArgsAndLocals];
	
//	{
//		u16 i = GetFunctionNumArgsAndLocals (o->function);
//		while (i < o->stackIndex)
//		{
//			u16 location = o->wasmStack [i];
//			if (IsRegisterLocation (location))
//			{
//				printf ("deal! %d - %d \n", (i32) i, (i32) location);
////				abort ();
////				d_m3Assert(false);
//			}
//			++i;
//		}
//	}
																				d_m3Assert (not IsRegisterAllocated (o, 0));
																				d_m3Assert (not IsRegisterAllocated (o, 1));
	
	memcpy (locals, o->wasmStack, numArgsAndLocals * sizeof (u16));
	for (u32 i = 0; i < numArgsAndLocals; ++i)
	{
//		printf ("enter -- %d local: %d \n", (i32) i, (i32) o->wasmStack [i]);
	}

	memset (o->wasmStack, 0, numArgsAndLocals * sizeof (u16));
	
_	(Compile_BlockScoped (o, i_blockType, i_blockOpcode));
	
	for (u32 i = 0; i < numArgsAndLocals; ++i)
	{
		if (o->wasmStack [i])
		{
//			printf ("modified: %d \n", (i32) i);
			u16 preserveToSlot;
_			(IsLocalReferencedWithCurrentBlock (o, & preserveToSlot, i));
			
			if (preserveToSlot != i)
			{
				printf ("preserving local: %d to slot: %d\n", i, preserveToSlot);
					abort (); // implement
			}
		}
		
		o->wasmStack [i] += locals [i];
	
//		printf ("local usage: [%d] = %d\n", i, o->wasmStack [i]);
	}
	

	catch: return result;
}


M3Result  Compile_ElseBlock  (IM3Compilation o, pc_t * o_startPC, u8 i_blockType)
{
	M3Result result;
	
	IM3CodePage elsePage = AcquireCodePage (o->runtime);
	
	if (elsePage)
	{
		* o_startPC = GetPagePC (elsePage);

		IM3CodePage savedPage = o->page;
		o->page = elsePage;
	
_		(Compile_Block (o, i_blockType, c_waOp_else));
		
_		(EmitOp (o, op_Branch));
		EmitPointer (o, GetPagePC (savedPage));

		o->page = savedPage;
	}
	else throw (c_m3Err_mallocFailedCodePage);
	
	catch: return result;
}


M3Result  CompileLocals  (IM3Compilation o)
{
	M3Result result;
	
	u32 numLocalBlocks;
_	(ReadLEB_u32 (& numLocalBlocks, & o->wasm, o->wasmEnd));
	
	for (u32 l = 0; l < numLocalBlocks; ++l)
	{
		u32 varCount;
		i8 waType;
		u8 localType;

_		(ReadLEB_u32 (& varCount, & o->wasm, o->wasmEnd));
_		(ReadLEB_i7 (& waType, & o->wasm, o->wasmEnd));
_		(NormalizeType (& localType, waType));
																								m3log (compile, "%d locals: %s", varCount, c_waTypes [localType]);
		while (varCount--)
_			(PushAllocatedSlot (o, localType));
	}
	
	catch: return result;
}


M3Result  Compile_ReserveConstants  (IM3Compilation o)
{
	M3Result result = c_m3Err_none;
	
	// in the interest of speed, this blindly scans the Wasm code looking for any byte
	// that looks like an const opcode.
	u32 numConstants = 0;
	
	bytes_t wa = o->wasm;
	while (wa < o->wasmEnd)
	{
		u8 code = * wa++;
		
		if (code >= 0x41 and code <= 0x44)
			++numConstants;
	}																							m3log (compile, "estimated constants: %d", numConstants)
	
	o->firstSlotIndex = o->firstConstSlotIndex = o->constSlotIndex = o->stackIndex;
	
	// if constants overflow their reserved stack space, the compiler simply emits op_Const
	// operations as needed. Compiled expressions (global inits) don't pass through this
	// ReserveConstants function and thus always prdouce inline contants.
	numConstants = min (numConstants, c_m3MaxNumFunctionConstants);
	
	u32 freeSlots = c_m3MaxFunctionStackHeight - o->constSlotIndex;
	
	if (numConstants <= freeSlots)
		o->firstSlotIndex += numConstants;
	else
		result = c_m3Err_functionStackOverflow;

	return result;
}


M3Result  Compile_Function  (IM3Function io_function)
{
	M3Result result = c_m3Err_none;										m3log (compile, "compiling: '%s'; wasm-size: %d; numArgs: %d; return: %s",
																		   io_function->name, (u32) (io_function->wasmEnd - io_function->wasm), GetFunctionNumArgs (io_function), c_waTypes [GetFunctionReturnType (io_function)]);
	IM3Runtime rt = io_function->module->runtime;
	
	M3Compilation o = { rt, io_function->module, io_function->wasm, io_function->wasmEnd };

	o.page = AcquireCodePage (rt);
	
	if (o.page)
	{
		pc_t pc = GetPagePC (o.page);

		o.block.type = GetFunctionReturnType (io_function);
		o.function = io_function;

		// push the arg types to the type stack
		M3FuncType * ft = io_function->funcType;
		
		for (u32 i = 0; i < GetFunctionNumArgs (io_function); ++i)
		{
			u8 type = ft->argTypes [i];
_			(PushAllocatedSlot (& o, type));
		}
		
_		(CompileLocals (& o));
		
		// the stack for args/locals is used to track # of Sets
		for (u32 i = 0; i < o.stackIndex; ++i)
			o.wasmStack [i] = 0;

_		(Compile_ReserveConstants (& o));

		o.numAllocatedExecSlots = 0;				// this var only tracks dynamic slots so clear local+constant allocations
		o.block.initStackIndex = o.stackIndex;
		
		pc_t pc2 = GetPagePC (o.page);
		assert (pc2 == pc);
		
_		(EmitOp (& o, op_Entry));//, comp.stackIndex);
		EmitPointer (& o, io_function);
		
_		(Compile_BlockStatements (& o));
		
		io_function->compiled = pc;
		
		u32 numConstants = o.constSlotIndex - o.firstConstSlotIndex;
		
		io_function->numConstants = numConstants;					m3log (compile, "unique constants: %d unused slots: %d", numConstants, o.firstSlotIndex - o.constSlotIndex);
		
		if (numConstants)
		{
_			(m3Alloc (& io_function->constants, u64, numConstants));
			
			memcpy (io_function->constants, o.constants, sizeof (u64) * numConstants);
		}
	}
	else throw (c_m3Err_mallocFailedCodePage);
	
	catch:
	{
		ReleaseCodePage (rt, o.page);
	}

	return result;
}






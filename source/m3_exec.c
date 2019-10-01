//
//  m3_exec.c
//  M3: Massey Meta Machine
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_exec.h"
#include "m3_compile.h"


m3ret_t ReportOutOfBoundsMemoryError (pc_t i_pc, u8 * i_mem, u32 i_offset)
{
	M3MemoryHeader * info = (M3MemoryHeader *) (i_mem - sizeof (M3MemoryHeader));
	u8 * mem8 = i_mem + i_offset;
	
	ErrorModule (c_m3Err_trapOutOfBoundsMemoryAccess, info->module, "memory bounds: [%p %p); accessed: %p; offset: %u overflow: %lld bytes", i_mem, info->end, mem8, i_offset, mem8 - (u8 *) info->end);
	
	return c_m3Err_trapOutOfBoundsMemoryAccess;
}


void  ReportError2  (IM3Function i_function, M3Result i_result)
{
	i_function->module->runtime->runtimeError = i_result;
}


d_m3OpDef  (Call)
{
	pc_t callPC 				= immediate (pc_t);
	i32 stackOffset				= immediate (i32);

	m3stack_t sp = _sp + stackOffset;
	
	m3ret_t r = Call (callPC, sp, _mem, d_m3OpDefaultArgs);
	
	if (r == 0)
		r = nextOp ();
	
	return r;
}


// TODO: type not checked.
d_m3OpDef  (CallIndirect)
{
	IM3Module module			= immediate (IM3Module);
	IM3FuncType type			= immediate (IM3FuncType);
	i32 stackOffset				= immediate (i32);

	m3stack_t sp = _sp + stackOffset;

	i32 tableIndex = * (i32 *) (sp + type->numArgs);
	
	if (tableIndex >= 0 and tableIndex < module->table0Size)
	{
		m3ret_t r = c_m3Err_none;
		
		IM3Function function = module->table0 [tableIndex];
		
		if (function)
		{
			if (not function->compiled)
				r = Compile_Function (function);

			if (not r)
			{
				r = Call (function->compiled, sp, _mem, d_m3OpDefaultArgs);
				
				if (not r)
					r = nextOp ();
			}
		}
		else r = "trap: table element is null";
			
		return r;
	}
	else return c_m3Err_trapTableIndexOutOfRange;
}


// it's a debate: should the compilation be trigger be the caller or callee page.
// it's a much easier to put it in the caller pager. if it's in the callee, either the entire page
// has be left dangling or it's just a stub that jumps to a newly acquire page.  In Gestalt, I opted
// for the stub approach. Stubbing makes it easier to dynamically free the compilation. You can also
// do both.
d_m3OpDef  (Compile)
{
	rewrite (op_Call);
	
	IM3Function function		= immediate (IM3Function);

	M3Result result = c_m3Err_none;
	
	if (not function->compiled)	// check to see if function was compiled since this operation was emitted.
		result = Compile_Function (function);
	
	if (not result)
	{
		// patch up compiled pc and call rewriten op_Call
		*((m3word_t *) --_pc) = (m3word_t) (function->compiled);
		--_pc;
		result = nextOp ();
	}
	else ReportError2 (function, result);
	
	return result;
}



d_m3OpDef  (Entry)
{
	IM3Function function = immediate (IM3Function);
	function->hits++;										m3log (exec, " enter > %s %s", function->name, SPrintFunctionArgList (function, _sp));

	u32 numLocals = function->numLocals;
	
	m3stack_t stack = _sp + GetFunctionNumArgs (function);
	while (numLocals--)										// it seems locals need to init to zero (at least for optimized Wasm code)
		* (stack++) = 0;
	
	memcpy (stack, function->constants, function->numConstants * sizeof (u64));

	m3ret_t r = nextOp ();
	
	if (d_m3Log_exec)
	{
		u8 returnType = function->funcType->returnType;
	
		char str [100] = { '!', 0 };
		
		if (not r)
			SPrintArg (str, 99, _sp, function->funcType->returnType);
			
		m3log (exec, " exit < %s %s %s   %s\n", function->name, returnType ? "->" : "", str, r ? r : "");
	}

	return r;
}



d_m3OpDef  (DumpStack)
{
	u32 opcodeIndex			= immediate (u32);
	u64 stackHeight 		= immediate (u64);
	IM3Function function	= immediate (IM3Function);
	
	cstr_t funcName = (function) ? function->name : "";
	
	printf (" %4d ", opcodeIndex);
	printf (" %-25s     r0: 0x%016llx  i:%lld  u:%llu  \n", funcName, _r0, _r0, _r0);
	printf ("                                     fp0: %lf  \n", _fp0);
	
	u64 * sp = _sp;
	
	for (u32 i = 0; i < stackHeight; ++i)
	{
		printf ("%016llx  ", (u64) sp);
		
		cstr_t kind = "";
		
		printf ("%5s  %2d: 0x%llx %lld\n", kind, i, (u64) *(sp), (i64) *sp);
		
		++sp;
	}
	printf ("---------------------------------------------------------------------------------------------------------\n");
	
	return Op (_pc, d_m3OpArgs);
}




# if d_m3EnableOpProfiling
M3ProfilerSlot s_opProfilerCounts [c_m3ProfilerSlotMask] = {};


void  ProfileHit  (cstr_t i_operationName)
{
	u64 ptr = (u64) i_operationName;
	
	M3ProfilerSlot * slot = & s_opProfilerCounts [ptr & c_m3ProfilerSlotMask];
	
	if (slot->opName)
	{
		if (slot->opName != i_operationName)
		{
			printf ("**** profiler slot collision; increase mask width\n");
			abort ();
		}
	}
	
	slot->opName = i_operationName;
	slot->hitCount++;
}
# endif


void  m3_PrintProfilerInfo  ()
{
	# if d_m3EnableOpProfiling
		for (u32 i = 0; i <= c_m3ProfilerSlotMask; ++i)
		{
			M3ProfilerSlot * slot = & s_opProfilerCounts [i];
			
			if (slot->opName)
				printf ("%13llu  %s\n", slot->hitCount, slot->opName);
		}
	# endif
}



d_m3OpDef  (Loop)
{
	m3ret_t r;
	
	do
	{
		r = nextOp ();				// printf ("loop: %p\n", r);
	}
	while (r == _pc);

	return r;
}


d_m3OpDef  (If_r)
{
	i32 condition = (i32) _r0;
	
	immediate (pc_t);						// empty preservation chain
	
	pc_t elsePC = immediate (pc_t);
	
	if (condition)
		return nextOp ();
	else
		return d_else (elsePC);
}


d_m3OpDef  (If_s)
{
	i32 condition = slot (i32);
	
	immediate (pc_t);						// empty preservation chain
	
	pc_t elsePC = immediate (pc_t);
	
	if (condition)
		return nextOp ();
	else
		return d_else (elsePC);
}


d_m3OpDef  (IfPreserve)
{
	i32 condition = (i32) _r0;
	
	d_call (immediate (pc_t));
	
	pc_t elsePC = immediate (pc_t);			//printf ("else: %p\n", elsePC);
	
	if (condition)
		return nextOp ();
	else
		return d_else (elsePC);
}


d_m3OpDef  (Trap)
{													m3log (exec, "*** trapping ***");
	return c_m3Err_runtimeTrap;
}


d_m3OpDef  (End)
{
	return 0;
}


d_m3OpDef (i32_Remainder)
{
	abort (); // fix
	
	if (_r0 != 0)
	{
		i32 op0 = * ((i32 *) --_sp);
		
		// max negative divided by -1 overflows max positive
		if (op0 == 0x80000000 and _r0 == -1)
			_r0 = 0;
		else
			_r0 = op0 % _r0;
	
		return nextOp ();
	}
	else return c_m3Err_trapRemainderByZero;
}


//inline
m3ret_t Remainder_u32 (m3reg_t * o_result, u32 i_op1, u32 i_op2)
{
	if (i_op2 != 0)
	{
		//		// max negative divided by -1 overflows max positive
		//		if (op0 == 0x80000000 and _r0 == -1)
		//			_r0 = 0;
		//		else
		//			_r0 = op0 % _r0;
		
		u32 result = i_op1 % i_op2;
		
		* o_result = result;
		
		return c_m3Err_none;
	}
	else return c_m3Err_trapRemainderByZero;
}


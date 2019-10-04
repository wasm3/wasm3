//
//  m3_compile.h
//  M3: Massey Meta Machine
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_compile_h
#define m3_compile_h

#include "m3_env.h"
#include "m3_exec_defs.h"

/*
 
 WebAssembly spec info
 ---------------------
 2.3.2 Result Types
 Note: In the current version of WebAssembly, at most one value is allowed as a result. However, this may be
 generalized to sequences of values in future versions.

 M3
 --
 
*/

enum
{
	c_waOp_block				= 0x02,
	c_waOp_loop					= 0x03,
	c_waOp_if					= 0x04,
	c_waOp_else					= 0x05,
	c_waOp_end					= 0x0b,
	c_waOp_branch				= 0x0c,
	c_waOp_branchIf				= 0x0d,
	c_waOp_getLocal 			= 0x20,
	c_waOp_setLocal 			= 0x21,
	c_waOp_teeLocal 			= 0x22,
};

//-----------------------------------------------------------------------------------------------------------------------------------

// since the end location of a block can be unknown when a branch is compiled, writing
// the actual address must deferred. A linked-list of patch locations is kept in
// M3CompilationScope. When the block compilation exits, it patches these addresses.
typedef struct M3BranchPatch
{
	struct M3BranchPatch *			next;
	pc_t *							location;
}
M3BranchPatch;

typedef M3BranchPatch * 			IM3BranchPatch;


typedef struct M3CompilationScope
{
	struct M3CompilationScope *		outer;
	
	pc_t							pc;					// used by ContinueLoop's
	IM3BranchPatch					patches;
	i32								depth;
	i32								loopDepth;
	i16								initStackIndex;
	u8								type;
	u8								opcode;
}
M3CompilationScope;

typedef M3CompilationScope * 		IM3CompilationScope;


static const u16 c_m3RegisterUnallocated = 0;

typedef struct
{
	IM3Runtime			runtime;
	IM3Module			module;
	
	const u8 *			wasm;
	cbytes_t			wasmEnd;

	M3CompilationScope	block;

	IM3Function			function;
	
	IM3CodePage			page;

	u32					numEmits;
	u32					numOpcodes;
	
	u16 				firstSlotIndex;				// numArgs + numLocals + numReservedConstants. the first mutable slot available to the compiler.
	u16					stackIndex;					// current stack index
	
	u16					firstConstSlotIndex;
	u16					constSlotIndex;				// as const's are encountered during compilation this tracks their location in the "real" stack

	u64					constants					[c_m3MaxNumFunctionConstants];
	
	// for args/locals this wasmStack tracks write counts. for the dynamic portion of the stack, the array holds slot locations
	u16					wasmStack						[c_m3MaxFunctionStackHeight];
	u8					typeStack					[c_m3MaxFunctionStackHeight];

	// this array just contains single bit allocation flags.  could be fused with the typeStack to conserve space
	u8					m3Slots						[c_m3MaxFunctionStackHeight];
	
	u16					numAllocatedExecSlots;

	u16					regStackIndexPlusOne		[2];
	
	bool				enableOptimizations;		// no longer used. currently implementation is highly pre-optimized. 
	
	u8					previousOpcode;
}
M3Compilation;

typedef M3Compilation * 				IM3Compilation;
typedef M3Result (* M3Compiler)			(IM3Compilation, u8);


//-----------------------------------------------------------------------------------------------------------------------------------


typedef struct M3OpInfo
{
#ifdef DEBUG
	const char * const		name;
#endif
	
	i8						stackOffset;
	u8						type;

	IM3Operation			operation_sr;		// top operand in register
	IM3Operation			operation_rs;		// top operand in stack
	IM3Operation			operation_ss;		// both operands in stack
	
	M3Compiler				compiler;
}
M3OpInfo;

typedef const M3OpInfo *	IM3OpInfo;

extern const M3OpInfo c_operations [];

#ifdef DEBUG
	#define M3OP(...) { __VA_ARGS__ }
	#define M3OP_RESERVED { "reserved" }
#else
	#define M3OP(name, ...) { __VA_ARGS__ }
	#define M3OP_RESERVED { }
#endif
//-----------------------------------------------------------------------------------------------------------------------------------


M3Result	EmitOp						(IM3Compilation o, IM3Operation i_operation);
void 		EmitConstant				(IM3Compilation o, const u64 immediate);
void		Push						(IM3Compilation o, u8 i_waType, i16 i_location);
void 		EmitPointer					(IM3Compilation o, const void * const i_immediate);

M3Result	CompileBlock				(IM3Compilation io, u8 i_blockType, u8 i_blockOpcode);
M3Result	Compile_ElseBlock			(IM3Compilation io, pc_t * o_startPC, u8 i_blockType);

M3Result	Compile_BlockStatements		(IM3Compilation io);
M3Result	Compile_Function			(IM3Function io_function);

bool		PeekNextOpcode				(IM3Compilation o, u8 i_opcode);

//M3Result	Optimize_ConstOp 			(IM3Compilation o, u64 i_word, u8 i_waType);

#endif /* m3_compile_h */

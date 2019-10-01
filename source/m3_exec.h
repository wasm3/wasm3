//
//  m3_exec.h
//  M3: Massey Meta Machine
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.


#ifndef m3_exec_h
#define m3_exec_h

// TODO: all these functions could move over to the .c at some point. normally, i'd say screw it,
// but it might prove useful to be able to compile m3_exec alone w/ optimizations while the remaining
// code is at debug O0

#include "m3_exec_defs.h"

#include <math.h>

# define rewrite(NAME)				* ((void **) (_pc-1)) = NAME

# define d_m3RetSig					static inline m3ret_t vectorcall
# define d_m3Op(NAME)				d_m3RetSig op_##NAME (d_m3OpSig)

# define d_m3OpDef(NAME)			m3ret_t vectorcall op_##NAME (d_m3OpSig)
# define d_m3OpDecl(NAME)			d_m3OpDef (NAME);

# define immediate(TYPE) 			* ((TYPE *) _pc++)

# define slot(TYPE)					* (TYPE *) (_sp + immediate (i32))

# if d_m3EnableOpProfiling
# 	define nextOp()					profileOp (_pc, d_m3OpArgs, __PRETTY_FUNCTION__)
# endif

# if d_m3TraceExec
# 	define nextOp()					debugOp (d_m3OpAllArgs, __PRETTY_FUNCTION__)
# endif

# ifndef nextOp
# 	define nextOp()					Op (_pc, d_m3OpArgs)
# endif

#define d_call(PC)					Call (PC, d_m3OpArgs)

#define d_else(PC)					Else (PC, d_m3OpArgs)


d_m3RetSig  Call  (d_m3OpSig)
{
	IM3Operation operation = (* _pc);
	return operation (_pc + 1, d_m3OpArgs);
}

d_m3RetSig  Else  (d_m3OpSig)
{
	IM3Operation operation = (* _pc);
	return operation (_pc + 1, d_m3OpArgs);
}


d_m3RetSig Op (d_m3OpSig)
{
	IM3Operation operation = (* _pc);
	return operation (_pc + 1, d_m3OpArgs);
}


d_m3RetSig  debugOp  (d_m3OpSig, cstr_t i_opcode)
{
	char name [100];
	strcpy (name, strstr (i_opcode, "op_") + 3);
	* strstr (name, "(") = 0;
	
	printf ("%s\n", name);
	return Op (d_m3OpAllArgs);
}

static const u32 c_m3ProfilerSlotMask = 0xFFFF;

typedef struct M3ProfilerSlot
{
	cstr_t		opName;
	u64			hitCount;
}
M3ProfilerSlot;

void  ProfileHit  (cstr_t i_operationName);

d_m3RetSig  profileOp  (d_m3OpSig, cstr_t i_operationName)
{
	ProfileHit (i_operationName);
	
	return Op (_pc, d_m3OpArgs);
}


d_m3OpDecl  (DumpStack)



#define d_m3CompareOp(REG, TYPE, NAME, OPERATION) d_m3RetSig op_##TYPE##_##NAME##_sr (d_m3OpSig) \
{ 														\
	TYPE * stack = (TYPE *) (_sp + immediate (i32));	\
	_r0 = * stack OPERATION (TYPE) REG;					\
	return nextOp ();									\
}														\
														\
d_m3RetSig op_##TYPE##_##NAME##_ss(d_m3OpSig) 			\
{ 														\
	TYPE * stackB = (TYPE *) (_sp + immediate (i32));	\
	TYPE * stackA = (TYPE *) (_sp + immediate (i32));	\
	_r0 = * stackA OPERATION * stackB;					\
	return nextOp ();									\
}														\
														\
d_m3RetSig op_##TYPE##_##NAME##_rs (d_m3OpSig) 			\
{ 														\
	TYPE * stack = (TYPE *) (_sp + immediate (i32));	\
	_r0 = (TYPE) REG OPERATION (* stack);				\
	return nextOp ();									\
}														\

// compare needs to be distinct for fp 'cause the result must be _r0
#define d_m3CompareOp_f(TYPE, NAME, OP) 		d_m3CompareOp (_fp0, TYPE, NAME, OP)

//-----------------------

#define d_m3CommutativeOp(REG, TYPE, NAME, OPERATION) d_m3RetSig op_##TYPE##_##NAME##_sr (d_m3OpSig) \
{ 														\
	TYPE * stack = (TYPE *) (_sp + immediate (i32));	\
	REG = * stack OPERATION (TYPE) REG;					\
	return nextOp ();									\
}														\
														\
d_m3RetSig op_##TYPE##_##NAME##_ss(d_m3OpSig) 			\
{ 														\
	TYPE * stackB = (TYPE *) (_sp + immediate (i32));	\
	TYPE * stackA = (TYPE *) (_sp + immediate (i32));	\
	REG = * stackA OPERATION * stackB;					\
	return nextOp ();									\
}

#define d_m3Op_(REG, TYPE, NAME, OPERATION) d_m3RetSig op_##TYPE##_##NAME##_rs (d_m3OpSig) \
{ 														\
	TYPE * stack = (TYPE *) (_sp + immediate (i32));	\
	REG = (TYPE) REG OPERATION (* stack);				\
	return nextOp ();									\
}														\
d_m3CommutativeOp(REG, TYPE,NAME,OPERATION)


#define d_m3CommutativeOp_i(TYPE, NAME, OP) 		d_m3CommutativeOp	(_r0, TYPE, NAME, OP)
#define d_m3Op_i(TYPE, NAME, OP)					d_m3Op_				(_r0, TYPE, NAME, OP)
#define d_m3CommutativeOp_f(TYPE, NAME, OP) 		d_m3CommutativeOp	(_fp0, TYPE, NAME, OP)
#define d_m3Op_f(TYPE, NAME, OP)					d_m3Op_				(_fp0, TYPE, NAME, OP)



// signed
d_m3Op_i (i32, Equal,						==)		d_m3Op_i (i64, Equal,						==)
d_m3Op_i (i32, NotEqual,					!=)		d_m3Op_i (i64, NotEqual,					!=)

d_m3Op_i (i32, LessThan,					<)		d_m3Op_i (i64, LessThan,					<)
d_m3Op_i (i32, GreaterThan,					>)		d_m3Op_i (i64, GreaterThan,					>)
d_m3Op_i (i32, LessThanOrEqual,				<=)		d_m3Op_i (i64, LessThanOrEqual,				<=)
d_m3Op_i (i32, GreaterThanOrEqual,			>=)		d_m3Op_i (i64, GreaterThanOrEqual,			>=)

d_m3CompareOp_f (f32, Equal,				==)
d_m3CompareOp_f (f32, NotEqual,				!=)
d_m3CompareOp_f (f32, LessThan,				<)
d_m3CompareOp_f (f32, GreaterThan,			>)
d_m3CompareOp_f (f32, LessThanOrEqual,		<=)
d_m3CompareOp_f (f32, GreaterThanOrEqual,	>=)

d_m3CompareOp_f (f64, Equal,				==)
d_m3CompareOp_f (f64, NotEqual,				!=)
d_m3CompareOp_f (f64, LessThan,				<)
d_m3CompareOp_f (f64, GreaterThan,			>)
d_m3CompareOp_f (f64, LessThanOrEqual,		<=)
d_m3CompareOp_f (f64, GreaterThanOrEqual,	>=)

// unsigned
d_m3Op_i (u32, LessThan,					<)		d_m3Op_i (u64, LessThan,					<)
d_m3Op_i (u32, GreaterThan,					>)		d_m3Op_i (u64, GreaterThan,					>)
d_m3Op_i (u32, LessThanOrEqual,				<=)		d_m3Op_i (u64, LessThanOrEqual,				<=)
d_m3Op_i (u32, GreaterThanOrEqual,			>=)		d_m3Op_i (u64, GreaterThanOrEqual,			>=)


// are these supposed to trap? sounds like it
// "Signed and unsigned operators trap whenever the result cannot be represented in the result type."

d_m3CommutativeOp_i (i32, Add,				+)		d_m3CommutativeOp_i (i64, Add,				+)
d_m3CommutativeOp_i (i32, Multiply,			*)		d_m3CommutativeOp_i (i64, Multiply,			*)

d_m3Op_i (i32, Subtract, 					-)		d_m3Op_i (i64, Subtract,					-)

d_m3Op_i (i32, Remainder,					%)

// TODO: trap division by zero
d_m3Op_i (i32, Divide,						/)		d_m3Op_i (i64, Divide,						/)
d_m3Op_i (u32, Divide,						/)		d_m3Op_i (u64, Divide,						/)

d_m3OpDecl (i32_Remainder)

d_m3Op_i (i32, ShiftLeft,					<<)		d_m3Op_i (i64, ShiftLeft,					<<)
d_m3Op_i (i32, ShiftRight,					>>)		d_m3Op_i (i64, ShiftRight,					>>)
d_m3Op_i (u32, ShiftRight,					>>)		d_m3Op_i (u64, ShiftRight,					>>)

d_m3CommutativeOp_i (u64, And,				&)
d_m3CommutativeOp_i (u64, Or,				|)
d_m3CommutativeOp_i (u64, Xor,				^)

d_m3Op_f (f32, Add, 						+)		d_m3Op_f (f64, Add, 						+)
d_m3Op_f (f32, Subtract,				 	-)		d_m3Op_f (f64, Subtract,	 				-)
d_m3Op_f (f32, Multiply, 					*)		d_m3Op_f (f64, Multiply, 					*)






#define d_m3CommutativeFuncOp(REG, TYPE, NAME, FUNC) d_m3RetSig op_##TYPE##_##NAME##_sr (d_m3OpSig) \
{ 														\
	TYPE * stack = (TYPE *) (_sp + immediate (i32));	\
	m3ret_t r = FUNC (& REG, * stack, (TYPE) REG);		\
	if (r) return r;									\
	else return nextOp ();								\
}														\
														\
d_m3RetSig op_##TYPE##_##NAME##_ss(d_m3OpSig) 			\
{ 														\
	TYPE * stackB = (TYPE *) (_sp + immediate (i32));	\
	TYPE * stackA = (TYPE *) (_sp + immediate (i32));	\
	m3ret_t r = FUNC (& REG, * stackA, * stackB);		\
	if (r) return r;									\
	else return nextOp ();								\
}

#define d_m3FuncOp_(REG, TYPE, NAME, FUNC) d_m3RetSig op_##TYPE##_##NAME##_rs (d_m3OpSig) \
{ 														\
	TYPE * stack = (TYPE *) (_sp + immediate (i32));	\
	m3ret_t r = FUNC (& REG, * stack, (TYPE) REG);		\
	if (r) return r;									\
	else return nextOp ();								\
}														\
d_m3CommutativeFuncOp(REG, TYPE, NAME, FUNC)


#define d_m3FuncOp_i(TYPE, NAME, FUNC)					d_m3FuncOp_ 	(_r0, TYPE, NAME, FUNC)


//inline
m3ret_t Remainder_u32 (m3reg_t * o_result, u32 i_op1, u32 i_op2);
//{
//	if (i_op2 != 0)
//	{
//		//		// max negative divided by -1 overflows max positive
//		//		if (op0 == 0x80000000 and _r0 == -1)
//		//			_r0 = 0;
//		//		else
//		//			_r0 = op0 % _r0;
//
//		u32 result = i_op1 % i_op2;
//
//		* o_result = result;
//
//		return c_m3Err_none;
//	}
//	else return c_m3Err_trapRemainderByZero;
//}

d_m3FuncOp_i (u32, Remainder, Remainder_u32);



/*
#define d_m3Op_Divide_f(TYPE, NAME, OPERATION) d_m3RetSig op_##TYPE##_##NAME (d_m3OpSig) \
{ 														\
	if (_fp0 != 0.)										\
	{													\
		f64 op0 = * ((f64 *) --_sp);					\
		_fp0 = (TYPE) op0 OPERATION (TYPE) _fp0;		\
		return nextOp ();								\
	}													\
	else return c_m3Trap_divisionByZero;				\
}

d_m3Op_Divide_f (f32, Divide, 				/)
d_m3Op_Divide_f (f64, Divide, 				/)
*/

d_m3Op_f (f32, Divide, 			/)
d_m3Op_f (f64, Divide, 			/)


#define d_m3BinaryOp2(TYPE, NAME, OPERATION) d_m3RetSig op_##TYPE##_##NAME (d_m3OpSig) \
{ 										\
	TYPE op1 = * ((TYPE *) --_sp);		\
	TYPE * op0 = (TYPE *) (_sp - 1);	\
	* op0 = OPERATION (*op0, op1);		\
abort ();\
	return nextOp ();					\
}

static inline f32 min32 (f32 a, f32 b) { return a < b ? a : b; }	d_m3BinaryOp2 (f32, Min, min32);
static inline f64 min64 (f64 a, f64 b) { return a < b ? a : b; }	d_m3BinaryOp2 (f64, Min, min64);
static inline f32 max32 (f32 a, f32 b) { return a > b ? a : b; }	d_m3BinaryOp2 (f32, Max, max32);
static inline f64 max64 (f64 a, f64 b) { return a > b ? a : b; }	d_m3BinaryOp2 (f64, Max, max64);

d_m3BinaryOp2 (f32, CopySign, copysignf);	d_m3BinaryOp2 (f64, CopySign, copysign);


#define d_m3UnaryOp2(TYPE, NAME, OPERATION) d_m3RetSig op_##TYPE##_##NAME (d_m3OpSig) \
{ 											\
	abort (); \
	TYPE * op = (TYPE *) (_sp - 1);			\
	* op = OPERATION (* op);				\
	return nextOp ();						\
}


d_m3UnaryOp2 (f32, Abs, fabsf);				d_m3UnaryOp2 (f64, Abs, fabs);
d_m3UnaryOp2 (f32, Ceil, ceilf);			d_m3UnaryOp2 (f64, Ceil, ceil);
d_m3UnaryOp2 (f32, Floor, floorf);			d_m3UnaryOp2 (f64, Floor, floor);
d_m3UnaryOp2 (f32, Trunc, truncf);			d_m3UnaryOp2 (f64, Trunc, trunc);
d_m3UnaryOp2 (f32, Sqrt, sqrtf);			d_m3UnaryOp2 (f64, Sqrt, sqrt);


// "unary"
#define d_m3UnaryOp_i(TYPE, NAME, OPERATION) d_m3RetSig op_##TYPE##_##NAME##_r (d_m3OpSig) \
{ 													\
	_r0 = (TYPE) _r0 OPERATION;						\
	return nextOp ();								\
} 													\
d_m3RetSig op_##TYPE##_##NAME##_s (d_m3OpSig)		\
{ 													\
	TYPE * stack = (TYPE *) (_sp + immediate (i32));\
	_r0 = * stack OPERATION;						\
	return nextOp ();								\
}


d_m3UnaryOp_i (i32, EqualToZero, == 0)
d_m3UnaryOp_i (i64, EqualToZero, == 0)


#define d_m3IntToFpConvertOp(TO, NAME, FROM) d_m3RetSig op_##TO##_##NAME##_##FROM##_r (d_m3OpSig) \
{ 															\
	_fp0 = (TO) ((FROM) _r0);								\
	return nextOp ();										\
} 															\
															\
d_m3RetSig op_##TO##_##NAME##_##FROM##_s (d_m3OpSig) 		\
{ 															\
	FROM * stack = (FROM *) (_sp + immediate (i32));		\
	_fp0 = (TO) (* stack);									\
	return nextOp ();										\
}


d_m3IntToFpConvertOp (f64, Convert, i32);
//d_m3IntToFpConversionOp (f64, Convert, u32);
//d_m3IntToFpConversionOp (f64, Convert, i64);
//d_m3IntToFpConversionOp (f64, Convert, u64);


#define d_m3FpToFpConvertOp(TO, NAME) d_m3RetSig op_##TO##_##NAME##_r (d_m3OpSig) \
{ 															\
	_fp0 = (TO) _fp0;										\
	return nextOp ();										\
} 															\
															\
d_m3RetSig op_##TO##_##NAME##_s (d_m3OpSig) 				\
{ 															\
	f64 * stack = (f64 *) (_sp + immediate (i32));			\
	_fp0 = (TO) (* stack);									\
	return nextOp ();										\
}


d_m3FpToFpConvertOp (f32, Demote)

d_m3Op  (Extend_u)
{
	_r0 = (u32) _r0;
	return nextOp ();
}

d_m3Op  (Extend_s)
{
	_r0 = (i32) _r0;
	return nextOp ();
}


d_m3Op  (Nop)
{
	return nextOp ();
}


#define d_m3TruncateOp(TO, NAME, FROM, TEST) d_m3RetSig op_##TO##_##NAME##_##FROM (d_m3OpSig) \
{ 												\
	FROM from = _fp0;							\
	if (TEST (from))							\
	{ 											\
		_r0 = (TO) from;						\
		return nextOp ();						\
	}											\
	else return c_m3Err_trapTruncationOverflow;	\
}

/*
 FIX: should trap
 Truncation from floating point to integer where IEEE 754-2008 would specify an invalid operator exception
 (e.g. when the floating point value is NaN or outside the range which rounds to an integer in range) traps.

 */

static inline bool IsValid_i32 (f64 i_value)
{
	if (not isnan (i_value))
	{
		if (i_value >= INT32_MIN and i_value <= INT32_MAX)
			return true;
	}
	
	return false;
}


d_m3TruncateOp  (i32, Truncate, f64, IsValid_i32);


d_m3Op  (Block)
{
	return nextOp ();
}



d_m3OpDecl  (Loop)
d_m3OpDecl 	(If_r)
d_m3OpDecl 	(If_s)


d_m3Op  (Select_i_ssr)
{
 	i32 condition = (i32) _r0;
	
	i64 operand2 = * (_sp + immediate (i32));
	i64 operand1 = * (_sp + immediate (i32));

	_r0 = (condition) ? operand1 : operand2;

	return nextOp ();
}

d_m3Op  (Select_i_srs)
{
	i32 condition = (i32) * (_sp + immediate (i32));
	
	i64 operand2 = _r0;
	i64 operand1 = * (_sp + immediate (i32));
	
	_r0 = (condition) ? operand1 : operand2;
	
	return nextOp ();
}


d_m3Op  (Select_i_rss)
{
	i32 condition = (i32) * (_sp + immediate (i32));
	
	i64 operand2 = * (_sp + immediate (i32));
	i64 operand1 = _r0;
	
	_r0 = (condition) ? operand1 : operand2;
	
	return nextOp ();
}


d_m3Op  (Select_i_sss)
{
	i32 condition = (i32) * (_sp + immediate (i32));
	
	i64 operand2 = * (_sp + immediate (i32));
	i64 operand1 = * (_sp + immediate (i32));

	_r0 = (condition) ? operand1 : operand2;
	
	return nextOp ();
}


d_m3Op  (Select_f)
{
	i32 condition = (i32) _r0;
	
	f64 operand2 = * (f64 *) (_sp + immediate (i32));
	f64 operand1 = * (f64 *) (_sp + immediate (i32));
	
	_fp0 = (condition) ? operand1 : operand2;

	return nextOp ();
}


d_m3Op  (Return)
{
	return 0;
}


d_m3Op  (Branch)
{
	return d_call (* _pc);
}


d_m3Op  (Bridge)
{
	return d_call (* _pc);
}


d_m3Op  (BranchIf)
{
	i32 condition	= (i32) _r0;
	pc_t branch 	= immediate (pc_t);

	if (condition)
	{
		return d_call (branch);
	}
	else return nextOp ();
}


d_m3Op  (BranchTable)
{
	i32 index		= (i32) _r0;
	u32 numTargets	= immediate (u32);
	
	pc_t * branches = (pc_t *) _pc;
	
	if (index < 0 or index > numTargets)
		index = numTargets;	// the default index

	return d_call (branches [index]);
}


d_m3Op  (ContinueLoop)
{
	// TODO: this is where execution can "escape" the M3 code and callback to the client / fiber switch
	// OR it can go in the Loop operation
	
	void * loopId = immediate (void *);
	return loopId;
}


d_m3Op  (ContinueLoopIf)
{
	i32 condition = (i32) _r0;
	void * loopId = immediate (void *);
	
	if (condition)
	{
		return loopId;
	}
	else return nextOp ();
}



d_m3OpDecl	(Compile)
d_m3OpDecl  (Call)
d_m3OpDecl  (CallIndirect)
d_m3OpDecl	(Entry)


d_m3Op  (Const)
{
	u64 constant = immediate (u64);
	i32 offset = immediate (i32);
	* (_sp + offset) = constant;
	
	return nextOp ();
}


d_m3OpDecl  (Trap)

d_m3OpDecl  (End)


d_m3Op  (GetGlobal)
{
	i64 * global = immediate (i64 *);
	
//	printf ("get global: %p %lld\n", global, *global);
	
	i32 offset 	= immediate (i32);
	* (_sp + offset) = * global;
	
	return nextOp ();
}


d_m3Op  (SetGlobal_s)
{
	i64 * global = immediate (i64 *);
	i32 offset 	= immediate (i32);
	* global = * (_sp + offset);

	return nextOp ();
}


d_m3Op  (SetGlobal_i)
{
	i64 * global = immediate (i64 *);
	* global = _r0;
	
//	printf ("set global: %p %lld\n", global, _r0);

	return nextOp ();
}


d_m3Op  (SetGlobal_f)
{
	f64 * global = immediate (f64 *);
	* global = _fp0;
	
	return nextOp ();
}


d_m3Op (CopySlot)
{
	u64 * dst = _sp + immediate (i32);
	u64 * src = _sp + immediate (i32);

	* dst = * src;					// printf ("copy: %p <- %lld <- %p\n", dst, * dst, src);
	
	return nextOp ();
}


d_m3Op (PreserveCopySlot)
{
	u64 * dest = _sp + immediate (i32);
	u64 * src = _sp + immediate (i32);
	u64 * preserve = _sp + immediate (i32);

	* preserve = * dest;
	* dest = * src;
	
	return nextOp ();
}


d_m3Op  (SetRegister_i)
{
	i32 offset = immediate (i32);
	
	u64 * stack = _sp + offset;
	_r0 = * stack;
	
	return nextOp ();
}


d_m3Op  (SwapRegister_i)
{
	slot (u64) = _r0;
	_r0 = slot (u64);
	
	return nextOp ();
}


d_m3Op  (SetRegister_f)
{
	i32 offset = immediate (i32);
	
	f64 * stack = (f64 *) _sp + offset;
	_fp0 = * stack;
	
	return nextOp ();
}


d_m3Op (SetSlot_i)
{
	i32 offset = immediate (i32);
	
//	printf ("setslot_i %d\n", offset);
	
	u64 * stack = _sp + offset;
	* stack = _r0;
	
	return nextOp ();
}


d_m3Op (PreserveSetSlot_i)
{
	u64 * stack = (u64 *) _sp + immediate (i32);
	u64 * preserve = (u64 *) _sp + immediate (i32);
	* preserve = * stack;
	* stack = _r0;
	
	return nextOp ();
}


d_m3Op (SetSlot_f)
{
	i32 offset = immediate (i32);
	f64 * stack = (f64 *) _sp + offset;
	* stack = _fp0;
	
	return nextOp ();
}


d_m3Op (PreserveSetSlot_f)
{
	f64 * stack = (f64 *) _sp + immediate (i32);
	f64 * preserve = (f64 *) _sp + immediate (i32);
	* preserve = * stack;
	* stack = _fp0;

	return nextOp ();
}


#define d_outOfBounds return c_m3Err_trapOutOfBoundsMemoryAccess

m3ret_t ReportOutOfBoundsMemoryError (pc_t i_pc, u8 * i_mem, u32 i_offset);

//#define d_outOfBounds { return ReportOutOfBoundsMemoryError (_pc, _mem, operand); }


#define d_m3Load(REG,DEST_TYPE,SRC_TYPE) static inline m3ret_t vectorcall op_##DEST_TYPE##_Load_##SRC_TYPE##_r (d_m3OpSig) \
{ 														\
	u32 offset = immediate (u32);						\
	u32 operand = (u32) _r0;							\
														\
	u8 * src8 = _mem + operand + offset;				\
	u8 * end = * ((u8 **) _mem - 1);					\
														\
	if (src8 + sizeof (SRC_TYPE) <= end)				\
	{													\
		REG = (DEST_TYPE) (* (SRC_TYPE *) src8);		\
		return nextOp ();								\
	}													\
	else d_outOfBounds;									\
} 														\
static inline m3ret_t vectorcall op_##DEST_TYPE##_Load_##SRC_TYPE##_s (d_m3OpSig)		\
{ 														\
	u32 operand = * (u32 *) (_sp + immediate (i32));	\
	u32 offset = immediate (u32);						\
														\
	u8 * src8 = _mem + operand + offset;				\
	u8 * end = * ((u8 **) _mem - 1);					\
														\
	if (src8 + sizeof (SRC_TYPE) <= end)				\
	{													\
		REG = (DEST_TYPE) (* (SRC_TYPE *) src8);		\
		return nextOp ();								\
	}													\
	else d_outOfBounds;									\
}
//	printf ("get: %d -> %d\n", operand + offset, (i64) REG);				\


#define d_m3Load_i(DEST_TYPE, SRC_TYPE) d_m3Load(_r0, DEST_TYPE, SRC_TYPE)

d_m3Load_i (i32, i32);

d_m3Load_i (i32, i8);
d_m3Load_i (i32, u8);
d_m3Load_i (i32, i16);
d_m3Load_i (i32, u16);


d_m3Op  (f64_Store)
{
	u32 offset = immediate (u32);
	u32 operand = (u32) _r0;						//	printf ("store: %d\n", operand);
	u8 * mem8 = (u8 *) (_mem + operand + offset);
	* (f64 *) mem8 = _fp0;

	return nextOp ();
}


#define d_m3Store_i(SRC_TYPE, SIZE_TYPE) 				\
d_m3Op  (SRC_TYPE##_Store_##SIZE_TYPE##_sr)				\
{														\
	u32 operand = slot (u32);							\
	u32 offset = immediate (u32);						\
	operand += offset;									\
														\
	u8 * end = * ((u8 **) _mem - 1);					\
	u8 * mem8 = (u8 *) (_mem + operand);				\
														\
	if (mem8 + sizeof (SIZE_TYPE) <= end)				\
	{													\
		* (SIZE_TYPE *) mem8 = (SIZE_TYPE) _r0;			\
		return nextOp ();								\
	}													\
	else d_outOfBounds;									\
}														\
d_m3Op  (SRC_TYPE##_Store_##SIZE_TYPE##_rs)				\
{														\
	u32 operand = (u32) _r0;							\
	SRC_TYPE value = slot (SRC_TYPE);					\
	u32 offset = immediate (u32);						\
	operand += offset;									\
														\
	u8 * end = * ((u8 **) _mem - 1);					\
	u8 * mem8 = (u8 *) (_mem + operand);				\
														\
	if (mem8 + sizeof (SIZE_TYPE) <= end)				\
	{													\
		* (SIZE_TYPE *) mem8 = value;					\
		return nextOp ();								\
	}													\
	else d_outOfBounds;									\
}														\
d_m3Op  (SRC_TYPE##_Store_##SIZE_TYPE##_ss)				\
{														\
	u32 operand = slot (u32);							\
	SRC_TYPE value = slot (SRC_TYPE);					\
	u32 offset = immediate (u32);						\
	operand += offset;									\
														\
	u8 * end = * ((u8 **) _mem - 1);					\
	u8 * mem8 = (u8 *) (_mem + operand);				\
														\
	if (mem8 + sizeof (SIZE_TYPE) <= end)				\
	{													\
		* (SIZE_TYPE *) mem8 = value;					\
		return nextOp ();								\
	}													\
	else d_outOfBounds;									\
}


d_m3Store_i (i32, u8)
d_m3Store_i (i32, i32)
d_m3Store_i (i32, i16)


//---------------------------------------------------------------------------------------------------------------------
# if d_m3EnableOptimizations
//---------------------------------------------------------------------------------------------------------------------

	#define d_m3BinaryOpWith1_i(TYPE, NAME, OPERATION) d_m3RetSig op_##TYPE##_##NAME (d_m3OpSig) \
	{ 										\
		_r0 = _r0 OPERATION 1;			\
		return nextOp ();					\
	}

	d_m3BinaryOpWith1_i (u64, Increment,	+)
	d_m3BinaryOpWith1_i (u32, Decrement,	-)

	d_m3BinaryOpWith1_i (u32, ShiftLeft1, 	<<)
	d_m3BinaryOpWith1_i (u64, ShiftLeft1, 	<<)

	d_m3BinaryOpWith1_i (u32, ShiftRight1, 	>>)
	d_m3BinaryOpWith1_i (u64, ShiftRight1, 	>>)

//---------------------------------------------------------------------------------------------------------------------
# endif


#endif /* m3_exec_h */

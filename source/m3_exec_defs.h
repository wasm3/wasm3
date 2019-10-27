//
//  m3_exec_defs.h
//  m3
//
//  Created by Steven Massey on 5/1/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_exec_defs_h
#define m3_exec_defs_h

#include "m3_core.h"

// default Windows x64 calling convention doesn't have enough registers for M3. It only supports
// 4 args passed through registers but its enhanced __vectorcall calling convention does.

# if defined (M3_COMPILER_MSVC)
# 	define	vectorcall
# elif defined(WIN32)
# 	define	vectorcall __vectorcall
# elif defined (ESP8266)
#	include <c_types.h>
#	define vectorcall //ICACHE_FLASH_ATTR
# elif defined (ESP32)
# 	include "esp_system.h"
# 	define vectorcall IRAM_ATTR
# elif defined (FOMU)
# 	define vectorcall __attribute__((section(".ramtext")))
# elif defined(HIFIVE1)
#	include <metal/itim.h>
#   define vectorcall
#   define hotcall METAL_PLACE_IN_ITIM
# else
# 	define vectorcall
# endif


typedef f64 arch_f;
typedef i64 arch_i;


//---------------------------------------------------------------------------------------------------------------
#define c_m3NumIntRegisters 1
#define c_m3NumFpRegisters  1
#define c_m3NumRegisters    (c_m3NumIntRegisters + c_m3NumFpRegisters)

#	define d_m3OpSig 				pc_t _pc, u64 * _sp, u8 * _mem, m3reg_t _r0, f64 _fp0
#	define d_m3OpArgs	 			_sp, _mem, _r0, _fp0
#	define d_m3OpAllArgs	 		_pc, _sp, _mem, _r0, _fp0
#	define d_m3OpDefaultArgs		666, NAN


typedef m3ret_t (vectorcall * IM3Operation) (d_m3OpSig);



#endif /* m3_exec_defs_h */

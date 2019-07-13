//
//  m3_host.h
//  m3
//
//  Created by Steven Massey on 4/28/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_host_h
#define m3_host_h

//#include <inttypes.h>
#include "m3_core.h"

# if __cplusplus
extern "C" {
# endif

	void  	m3StdOut		(const char * const i_string);
	void	m3Export		(const void * i_data, int32_t i_size);
	double	TestReturn		(int32_t);
	void	m3Out_f64		(double i_value);
	void	m3Out_i32		(int32_t i_value);
	void	m3TestOut		(int32_t i_int, double i_double, int32_t i_int1);

	void	m3_printf		(cstr_t i_format, const void * i_varArgs);

	void	m3_abort		();

	i32		m3_malloc		(IM3Module i_module, i32 i_size);
	void	m3_free			(IM3Module i_module, i32 i_data);
	void *	m3_memset		(void * i_ptr, i32 i_value, i32 i_size);

	i32		m3_fread		(void * ptr, i32 size, i32 count, FILE * stream);


	i32		m3_fopen		(IM3Module i_module, ccstr_t i_path, ccstr_t i_mode);

# if __cplusplus
}
# endif

#endif /* m3_host_h */

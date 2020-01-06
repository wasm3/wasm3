//
//  m3_api_wasi.h
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#ifndef m3_api_wasi_h
#define m3_api_wasi_h

#include "m3_core.h"

# if defined(__cplusplus)
extern "C" {
# endif

    M3Result    m3_LinkWASI     (IM3Module io_module);

#if defined(__cplusplus)
}
# endif

#endif // m3_api_wasi_h

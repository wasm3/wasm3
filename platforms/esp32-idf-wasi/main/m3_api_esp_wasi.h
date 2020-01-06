//
//  m3_api_esp_wasi.h
//
//  Created by Volodymyr Shymanskyy on 01/07/20.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#ifndef m3_api_esp_wasi_h
#define m3_api_esp_wasi_h

#include "m3/m3_core.h"

# if defined(__cplusplus)
extern "C" {
# endif

    M3Result    m3_LinkEspWASI     (IM3Module io_module);

#if defined(__cplusplus)
}
# endif

#endif /* m3_api_esp_wasi_h */

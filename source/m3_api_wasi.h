//
//  m3_api_wasi.h
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#ifndef m3_api_wasi_h
#define m3_api_wasi_h

#include "m3_core.h"

#if defined(d_m3HasUVWASI)
#include "uvwasi.h"
#endif

d_m3BeginExternC

typedef struct m3_wasi_context_t
{
    i32                     exit_code;
    u32                     argc;
    ccstr_t *               argv;

#if defined(d_m3HasUVWASI)
    uvwasi_t uvwasi;
#endif
} m3_wasi_context_t;

#define M3WASI m3_wasi_context_t*

// ----------------------------------------------------------------------
// Free-standing WASI
// ----------------------------------------------------------------------

M3Result m3_NewCustomWASI(M3WASI*);
void m3_FreeCustomWASI(M3WASI);

M3Result    m3_LinkCustomWASI       (IM3Module, M3WASI);

#if defined(d_m3HasUVWASI)
M3Result    m3_LinkCustomWASIWithOptions  (IM3Module io_module, uvwasi_options_t uvwasiOptions);
#endif


// ----------------------------------------------------------------------
// Global WASI
// ----------------------------------------------------------------------
M3Result    m3_LinkWASI             (IM3Module io_module);

#if defined(d_m3HasUVWASI)
M3Result    m3_LinkWASIWithOptions        (IM3Module io_module, uvwasi_options_t uvwasiOptions);
#endif

m3_wasi_context_t* m3_GetWasiContext(void);


d_m3EndExternC

#endif // m3_api_wasi_h

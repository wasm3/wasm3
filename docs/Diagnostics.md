
## Logs

To enable various logs, modify the defines in `m3_config.h`.  These are only enabled when compiled in debug mode.

```C
# define d_m3LogParse           0   // .wasm binary decoding info
# define d_m3LogModule          0   // Wasm module info
# define d_m3LogCompile         0   // wasm -> metacode generation phase
# define d_m3LogWasmStack       0   // dump the wasm stack when pushed or popped
# define d_m3LogEmit            0   // metacode-generation info
# define d_m3LogCodePages       0   // dump metacode pages when released
# define d_m3LogRuntime         0   // higher-level runtime information
# define d_m3LogNativeStack     0   // track the memory usage of the C-stack
```


## Operation Profiling

To profile the interpreter's operations enable `d_m3EnableOpProfiling` in `m3_config.h`.  This profiling option works in either release or debug builds.

When a runtime is released or `m3_PrintProfilerInfo ()` is called, a table of the executed operations and 
their instance counts will be printed to stderr.

```
     23199904  op_SetSlot_i32
     12203917  op_i32_Add_ss
      6682992  op_u32_GreaterThan_sr
      2021555  op_u32_ShiftLeft_sr
      1136577  op_u32_ShiftLeft_ss
      1019725  op_CopySlot_32
       775431  op_i32_Subtract_ss
       703307  op_i32_Store_i32_rs
       337656  op_i32_Multiply_rs
       146383  op_u32_Or_rs
        99168  op_u64_Or_rs
        50311  op_u32_ShiftRight_rs
        43319  op_u32_ShiftLeft_rs
        21104  op_i64_Load_i64_s
        17450  op_i32_LessThan_sr
         7129  op_If_s
         5574  op_i32_Wrap_i64_r
         1630  op_f64_Load_f64_r
         1116  op_u32_Divide_sr
          903  op_i32_GreaterThan_ss
          390  op_u64_And_rs
          108  op_Select_f64_rss
           77  op_u64_ShiftLeft_sr
           64  op_Select_i32_ssr
           11  op_f64_Store_f64_ss
           10  op_MemGrow
            8  op_f64_GreaterThan_sr
            4  op_u64_LessThan_rs
            1  op_u32_Xor_ss
            1  op_i64_Subtract_ss
```


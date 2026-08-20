(module
  ;; https://github.com/wasm3/wasm3/issues/479
  ;;
  ;; Native C-stack exhaustion. op_Loop runs the loop body through a non-tail
  ;; call, so every nested loop costs a native stack frame while costing almost
  ;; no Wasm stack slots. Recursing through a deep nest therefore exhausts the
  ;; real C stack long before op_Entry's Wasm stack check can fire.
  ;;
  ;; Must trap with "stack overflow" (see d_m3MaxNativeStack), not segfault.

  (func $main
    loop loop loop loop loop loop loop loop
    loop loop loop loop loop loop loop loop
    loop loop loop loop loop loop loop loop
    loop loop loop loop loop loop loop loop
      call $main
    end end end end end end end end
    end end end end end end end end
    end end end end end end end end
    end end end end end end end end)

  (export "main" (func $main))
)

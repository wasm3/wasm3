# M3

This is a WebAssembly interpreter written in C using a novel, high performance interpreter topology. The interpreter strategy (named M3) was developed prior to this particular Wasm project and is described some below.

## Purpose

I don't know. I just woke up one day and started hacking this out after realizing my interpreter was well suited for the Wasm bytecode structure. Some ideas:

* It could be useful for embedded systems.
* It might be a good start-up, pre-JIT interpreter in a more complex Wasm compiler system.
* It could serve as a Wasm validation library.
* The interpreter topology might be inspiring to others.


## Benchmarks

It's at least fleshed out enough to run some simple benchmarks.

Tested on a 4 GHz Intel Core i7 iMac (Retina 5K, 27-inch, Late 2014).  M3 was compiled with Clang -Os.  C benchmarks were compiled with gcc with -O3

### Mandelbrot 

C code lifted from: https://github.com/ColinEberhardt/wasm-mandelbrot

|Interpreter|Execution Time|Relative to GCC|    |
|-----------|--------------|---------------|----|
|Life       |547 s         |133 x          | https://github.com/perlin-network/life 
|Lua        |122 s         |30 x           | This isn't Lua running some weird Wasm transcoding; a manual Lua conversion of the C benchmark as an additional reference point.
|M3         |17.9 s        |4.4 x          | (3.7 x on my 2016 MacBook Pro)
|GCC        |4.1 s         |               |

### CRC32

|Interpreter|Execution Time|Relative to GCC|
|-----------|--------------|---------------|
|Life       |153 s         |254 x          |
|M3         |5.1 s         |8.5 x          |
|GCC        |600 ms        |               |


In general, the M3 strategy seems capable of executing code around 4-15X slower than compiled code on a modern x86 processor.   (Older CPUs don't fare as well. I suspect branch predictor differences.)  I have yet to test on anything ARM.

## M3: Massey Meta Machine

Over the years, I've mucked around with creating my own personal programming language. It's called Gestalt. The yet unreleased repository will be here: https://github.com/soundandform/gestalt

Early on I decided I needed an efficient interpreter to achieve the instant-feedback, live-coding environment I desire.  Deep traditional compilation is too slow and totally unnecessary during development.  And, most importantly, compilation latency destroys creative flow.

I briefly considered retooling something extant.  The Lua virtual machine, one of the faster interpreters, is too Lua centric.  And everything else is too slow.

I've also always felt that the "spin in a loop around a giant switch statement" thing most interpreters do was clumsy and boring.  My intuition said there was something more elegant to be found.

The structure that emerged I named a "meta machine" since it mirrors the execution of compiled code much more closely than the switch-based virtual machine.  

I set out with a goal of approximating Lua's performance. But this interpreter appears to beat Lua by 3X and more on basic benchmarks -- whatever they're worth!  

### How it works

This rough information might not be immediately intelligible without referencing the source code.

#### Reduce bytecode decoding overhead

* Bytecode/opcodes are translated into more efficient "operations" during a compilation pass, generating pages of meta-machine code
    * M3 trades some space for time. Opcodes map to up to 3 different operations depending on the number of source operands and commutative-ness.
* Commonly occurring sequences of operations can can also be optimized into a "fused" operation.  This *sometimes* results in improved performance.
    * the modern CPU pipeline is a mysterious beast
* In M3/Wasm, the stack machine model is translated into a more direct and efficient "register file" approach.

#### Tightly Chained Operations

* M3 operations are C functions with a single, fixed function signature.

   ```C
   void * Operation_Whatever (pc_t pc, u64 * sp, u8 * mem, reg_t r0, f64 fp0);
   ```

* The arguments of the operation are the M3's virtual machine registers
    * program counter, stack pointer, etc.
* The return argument is a trap/exception and program flow control signal
* The M3 program code is traversed by each operation calling the next. The operations themselves drive execution forward. There is no outer control structure.
    * Because operations end with a call to the next function, the C compiler will tail-call optimize most operations.
* Finally, note that x86/ARM calling conventions pass initial arguments through registers, and the indirect jumps between operations are branch predicted.

#### The End Result

Since operations all have a standardized signature and arguments are tail-call passed through to the next, the M3 "virtual" machine registers end up mapping directly to real CPU registers.  It's not virtual! Instead, it's a meta machine with very low execution impedance.

|M3 Register                  |x86 Register|
|-----------------------------|------------|
|program counter (pc)         |rdi         |
|stack pointer (sp)           |rsi         |
|linear memory (mem)          |rdx         |
|integer register (r0)        |rcx         |
|floating-point register (fp0)|xmm0        |


For example, here's a bitwise-or operation in the M3 compiled on x86.  

```assembly
m3`op_u64_Or_sr:
    0x1000062c0 <+0>:  movslq (%rdi), %rax             ; load operand stack offset  
    0x1000062c3 <+3>:  orq    (%rsi,%rax,8), %rcx      ; or r0 with stack operand
    0x1000062c7 <+7>:  movq   0x8(%rdi), %rax          ; fetch next operation
    0x1000062cb <+11>: addq   $0x10, %rdi              ; increment program counter
    0x1000062cf <+15>: jmpq   *%rax                    ; jump to next operation
```


#### Registers and Operational Complexity

* The conventional Windows calling convention isn't compatible with M3, as-is, since it only passes 4 arguments through registers.  Applying the vectorcall calling convention (https://docs.microsoft.com/en-us/cpp/cpp/vectorcall) resolves this problem.

* It's possible to use more CPU registers. For example, adding an additional floating-point register to the meta-machine did marginally increase performance in prior tests.  However, the operation space increases exponentially.  With one register, there are up to 3 operations per opcode (e.g. a non-commutative math operation). Adding another register increases the operation count to 10.  However, as you can see above, operations tend to be tiny.

#### Stack Usage

* Looking at the above assembly code, you'll notice that once an M3 operation is optimized, it doesn't need the regular stack (no mucking with the ebp/esp registers).  This is the case for 90% of the opcodes.  Branching and call operations do require stack variables.  Therefore, execution can't march forward indefinitely; the stack would eventually overflow. 

   Therefore, loops unwind the stack.  When a loop is continued, the Continue operation returns, unwinding the stack.  Its return value is a pointer to the loop opcode it wants to unwind to.  The Loop operations checks for its pointer and responds appropriately, either calling back into the loop code or returning the loop pointer back down the call stack.

* Traps/Exceptions work similarly. A trap pointer is returned from the trap operation which has the effect of unwinding the entire stack.

* Returning from a Wasm function also unwinds the stack, back to the point of the Call operation. 

* But, because M3 execution leans heavily on the native stack, this does create one runtime usage issue.

   A conventional interpreter can save its state, break out of its processing loop and return program control to the client code.  This is not the case in M3 since the C stack might be wound up in a loop for long periods of time.

   With Gestalt, I resolved this problem with fibers (built with Boost Context).  M3 execution occurs in a fiber so that control can effortlessly switch to the "main" fiber.  No explicit saving of state is necessary since that's the whole purpose of a fiber.

   More simplistically, the interpreter runtime can also periodically call back to the client (in the either the Loop or LoopContinue operation).  This is necessary, regardless, to detect hangs and break out of infinite loops.


## The M3 strategy for other languages (is rad)

The Gestalt M3 interpreter works slightly differently than this Wasm version. With Gestalt, blocks of all kind (if/else/try), not just loops, unwind the native stack.  (This somewhat degrades raw x86 performance.)

But, this adds a really beautiful property to the interpreter.  The lexical scoping of a block in the language source code maps directly into the interpreter. All opcodes/operations end up having an optional prologue/epilogue structure.  This made things like reference-counting objects in Gestalt effortless. Without this property, the compiler itself would have to track scope and insert dererence opcodes intentionally.  Instead, the "CreateObject" operation is also the "DestroyObject" operation on the exit/return pathway.

Here's some pseudocode to make this more concrete:

```
return_t Operation_NewObject (registers...)
{
   Object o = runtime->CreateObject (registers...);
  
   * stack [index] = o;
  
   return_t r = CallNextOperation (registers...);  // executes to the end of the scope/block/curly-brace & returns
   
   if (o->ReferenceCount () == 0)
       runtime->DestroyObject (registers..., o);   // calls o's destructor and frees memory
   
   return r;
}
```

Likewise, a "defer" function (like in Go) becomes absolutely effortless to implement.  Exceptions (try/catch) as well.  


## Prior Art

After the Wasm3 project was posted to Hacker News (https://news.ycombinator.com/item?id=22024758), I finally discovered precedent for this tail-call interpreter design.  It has previously been called "threaded code". See the "Continuation-passing style" section: http://www.complang.tuwien.ac.at/forth/threaded-code.html).

If this style of interpreter was discussed back in the 70's, why hasn't it been more popular?  I suspect because there was no benefit until more recently.  Older calling conventions only used the stack to pass arguments, older CPUs didn't have branch prediction and compiler tail-call optimization maybe weren't ubiqutous.


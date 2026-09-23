# WebAssembly Gas Metering Proposal

**Title:** WebAssembly Deterministic Gas Metering and Execution Budgeting  
**Status:** Working Draft / Reference Specification  

---

## Abstract

This proposal specifies a standardized, host-agnostic mechanism for **deterministic gas metering and execution budgeting** in WebAssembly.

Gas metering assigns an execution cost to each WebAssembly instruction and enforces a predetermined resource ceiling on execution. When a computation exhausts its allocated gas budget, the runtime halts execution deterministically with an uncatchable `out of gas` trap, or cooperatively yields control to the host via execution suspension.

The specification defines:
1. An **abstract execution segmentation model** (basic block decomposition) with **prepayment semantics**, guaranteeing that runaway loops, unbounded recursions, and trapped segments are charged accurately before executing.
2. A **canonical micro-gas cost model** based on fixed-point integer units (10,000 units per gas) covering all core WebAssembly instructions and modern extensions (floating point, reference types, bulk memory, exceptions, and tail calls).
3. Semantics for **out-of-gas traps** and their integration with **stack switching, execution suspension, and process snapshots**.
4. An engine-agnostic **host embedder interface** and CLI conventions.
5. An **Implementation Notes** section providing concrete architectural guidance for interpreters, JIT compilers, and suspendable runtimes.

---

## Motivation and Use Cases

Standard WebAssembly execution is Turing-complete and unbounded. While sandboxed from host memory, an untrusted WebAssembly module can execute infinite loops, perform deep recursions, or consume excessive CPU time, leading to Denial of Service (DoS) in multi-tenant environments.

Host-side preemption techniques based on operating system signals, worker threads, or wall-clock timers are non-deterministic: the exact point of interruption varies across host CPU speeds, background system load, and thread scheduling. Furthermore, wall-clock timing is non-reproducible across different physical machines.

### Primary Use Cases

1. **Smart Contracts and Decentralized Compute:**
   Blockchains (e.g., Ethereum / ewasm, CosmWasm) require fully deterministic execution where every validator across a distributed network agrees on the exact cycle cost and termination point of every transaction.
2. **Untrusted Multi-Tenant Sandboxing & Serverless Edge:**
   Multi-tenant platforms running untrusted user functions need fine-grained, reproducible execution bounds without incurring the overhead of spinning up separate OS processes or containers.
3. **Deterministic Simulation & Time-Travel Debugging:**
   Simulators, game engines, and debuggers require cycle-exact execution accounting to reproduce past states and trace execution steps reliably.
4. **Stateful AI Agents & Interruptible Workflows:**
   Autonomous agents running complex multi-step reasoning can be assigned a discrete work budget per turn. When the budget is exhausted, the execution cleanly suspends, serializes its state to persistent storage (via snapshots), and resumes when scheduled again.
5. **Architectural-Independent Performance Profiling:**
   Gas counters serve as an instruction-level proxy for algorithmic computational complexity, independent of host CPU microarchitecture or memory latency.

---

## Comparison with Prior Art

| Capability / Property | Ahead-of-Time Bytecode Rewriting (e.g. `wasm-metering`) | Per-Instruction VM Counter (Fuel / Ticks) | Host Wall-Clock / Epoch Timers (e.g. Wasmtime Epochs) | Segment Prepayment Metering (This Proposal) |
|---|---|---|---|---|
| **Determinism** | **Deterministic** | **Deterministic** | **Non-deterministic** (clock / load dependent) | **Deterministic** |
| **Bytecode Integrity** | Mutates `.wasm` binary (injects synthetic globals/funcs) | Preserves original `.wasm` | Preserves original `.wasm` | **Preserves original `.wasm`** |
| **Debug Info & Offsets** | Breaks or desynchronizes DWARF / source maps | Preserves offsets | Preserves offsets | **Preserves exact offsets & DWARF** |
| **Runtime Overhead** | High (injected function calls and stack manipulations) | High (per-instruction decrements and branches) | Low (asynchronous signal / thread flag) | **Low** (one subtraction and branch per basic block) |
| **Tail Call Handling** | Often breaks or disrupts tail-call optimization | Bounded | Bounded | **Bounded per iteration via segment boundaries** |
| **Suspension & Snapshots** | Difficult (state polluted with injected locals/globals) | Complex (arbitrary mid-instruction states) | Asynchronous unwinding | **Native integration at block / safepoint boundaries** |

Prior art in WebAssembly gas metering largely focused on ahead-of-time bytecode instrumentation (rewriting WebAssembly binaries before execution to insert counting logic). While portable across stock engines, AOT rewriting significantly expands code size, alters function signatures, invalidates DWARF debug information, and adds measurable runtime call overhead.

This proposal specifies an **execution-level metering model** designed to be natively implemented by runtimes, ensuring zero bytecode modification and near-zero overhead.

---

## Abstract Execution Model & Segmentation

Gas metering decomposes every WebAssembly function body into a sequence of **straight-line execution segments** (basic blocks) and applies **prepayment accounting**.

### 1. Straight-Line Segments

A *straight-line segment* is a maximal sequence of instructions with single-entry, single-exit execution characteristics. Within a segment, instructions execute sequentially without internal branching, jumping, or external joins.

A segment begins at:
- Function entry.
- The target of any branch or jump (e.g., loop headers, `else` branches, blocks immediately following branches).
- The instruction immediately following a segment terminator.

### 2. Segment Terminators

An instruction is a *segment terminator* if it can alter the control flow or branch away from sequential execution. The following standard instructions terminate the current segment:

| Instruction Class | Instructions | Termination Rationale |
|---|---|---|
| **Loops** | `loop` | Marks the loop header; loop back-edges branch here. Closing the segment ensures loop iterations are charged on every pass. |
| **Conditionals** | `if`, `else` | Diverges execution into alternative control branches. |
| **Block Terminators** | `end` | Closes structured control blocks (`block`, `loop`, `if`). |
| **Unconditional Branches** | `br`, `return` | Unconditionally transfers control to a target label or caller. |
| **Conditional Branches** | `br_if` | Conditionally transfers control. |
| **Indirect Branches** | `br_table` | Branches indirectly via jump table index. |
| **Tail Calls** | `return_call`, `return_call_indirect`, `return_call_ref` | Replaces current activation frame and transfers control to target callee. Must terminate the segment to prevent unbounded unmetered tail-recursive loops. |
| **Exceptions** | `throw`, `throw_ref` | Unwinds activation frames to find matching exception handlers. |

### 3. Function Entry & Local Allocation

In addition to instructions, WebAssembly functions declare local variables that are initialized to default values (zeros or null references) upon activation:
- A function's declared locals incur an upfront initialization cost proportional to the number of declared locals (one unit per declared local).
- This charge is prepended to the function's initial entry segment.

### 4. Constant Expressions

Instructions executed as part of WebAssembly *constant expressions* (such as global variable initializers and table/data segment element offset expressions) run strictly during module instantiation before execution begins.
- Constant expressions are **not metered**.
- Metering applies exclusively to function bodies executed during runtime invocation.

### 5. Prepayment Accounting Semantics

To guarantee deterministic bounding and prevent resource exhaustion under abnormal exits:
1. The total cost of all instructions in a segment (plus any local initialization cost at function entry) is computed and aggregated.
2. The entire segment cost is **deducted upfront** upon entering the segment, before any instruction inside that segment executes.
3. If an instruction within the segment subsequently traps (e.g., integer division by zero, out-of-bounds memory access), the gas for the whole segment remains consumed.
4. If a segment's accumulated gas cost exceeds the maximum representable immediate limit ($2^{32} - 1$ units), the segment is split into consecutive prepayment checkpoints.

---

## Cost Model & Instruction Pricing

Gas accounting operates on a **canonical fixed-point integer unit** (10,000 units per gas) derived from the ewasm metering design. Operating in integer micro-units avoids all floating-point rounding discrepancies across different CPU architectures.
 
$$\text{Gas (fractional)} = \frac{\text{Gas Units}}{10\,000}$$
 
### Cost Tiers
 
The baseline instruction pricing is divided into distinct operational tiers reflecting computational and memory access complexity:
 
| Tier | Cost (Units) | Cost (Gas) | Description & Instruction Classes |
|---|---|---|---|
| **Nominal** | 1 | 0.0001 | Minimum bookkeeping: `nop`, `block`, `loop`, `if`, `try_table`, `ref.null`, `ref.func`, numeric constants (`i32.const`, `i64.const`, `f32.const`, `f64.const`). |
| **Local** | 1 | 0.0001 | Per declared local variable, charged once at function entry. |
| **Arith** | 45 | 0.0045 | Simple arithmetic & logic: integer/float additions, subtractions, multiplications, bitwise operations (`and`, `or`, `xor`), comparisons (`eq`, `ne`, `lt`, `gt`, etc.), bit counting (`clz`, `ctz`, `popcnt`), type conversions, sign extensions, saturating conversions, reference checks (`ref.is_null`, `ref.as_non_null`). |
| **Shift** | 67 | 0.0067 | Bit shifts: `i32.shl`, `i32.shr_s`, `i32.shr_u`, `i64.shl`, `i64.shr_s`, `i64.shr_u`. |
| **Branch** | 90 | 0.0090 | Direct control flow & calls: `br`, `br_if`, `else`, `return`, `call`, `return_call`, bit rotations (`rotl`, `rotr`), exception dispatch (`throw`, `throw_ref`). |
| **Query** | 100 | 0.0100 | Structural resource queries: `memory.size`, `table.size`, `data.drop`, `elem.drop`. |
| **Access** | 120 | 0.0120 | Storage reads & writes: local accesses (`local.get`, `local.set`, `local.tee`), global accesses (`global.get`, `global.set`), table accesses (`table.get`, `table.set`), linear memory loads and stores (all integer/float widths), `br_table`, `drop`, `select`. |
| **Heavy** | 10,000 | 1.0000 | Dynamic dispatch & bulk operations: indirect calls (`call_indirect`, `return_call_indirect`, `call_ref`, `return_call_ref`), memory bulk operations (`memory.init`, `memory.copy`, `memory.fill`, `memory.grow`), table bulk operations (`table.init`, `table.copy`, `table.grow`, `table.fill`). |
| **Divide** | 36,000 | 3.6000 | Heavy arithmetic: integer division and remainder (`i32/i64.div_s`, `div_u`, `rem_s`, `rem_u`), floating-point division and square root (`f32/f64.div`, `sqrt`). |
| **End** | 0 | 0.0000 | The `end` instruction is priced at zero units. |

### Extension Guidelines for Post-MVP Instructions

When engines implement additional WebAssembly proposals, instructions are categorized according to their closest behavioral analog:
1. **Typed References & GC:** Reference casting and type testing instructions (`ref.test`, `ref.cast`) map to the **Arith** tier. Struct/array field allocations map to **Heavy**, while field loads/stores map to **Access**.
2. **SIMD (128-bit):** Vector arithmetic, shuffles, and lane extractions map to **Arith**. Vector memory loads and stores map to **Access**.
3. **Stack Switching:** Continuation allocation (`cont.new`, `cont.bind`) maps to **Heavy**. Switching operations (`suspend`, `resume`, `switch`) map to **Branch**.

---

## Abstract Runtime Semantics

### 1. Store State

A metered runtime maintains two integer counters in its store:
- `gas_limit` (`s64`): The total budget allocated for the current execution invocation, expressed in gas units.
- `gas_remaining` (`s64`): The remaining gas units available for execution.

When armed with budget $G \ge 0$:
```text
gas_limit     := min(G * 10000, INT64_MAX)
gas_remaining := gas_limit
```

### 2. Segment Execution Rule

Before executing any straight-line segment $S$ with aggregated cost $\text{Cost}(S)$:
1. Deduct cost:
   ```text
   gas_remaining := gas_remaining - Cost(S)
   ```
2. Check boundary condition:  
   If `gas_remaining < 0`:
   - **Default Execution Mode:** Immediately abort execution and raise an uncatchable *out of gas* trap.
   - **Suspendable Execution Mode:** If the runtime supports execution suspension (e.g. stack switching or snapshotting) and is currently executing an interruptible continuation:
     1. Signal a pending suspension request:
        ```text
        suspend_requested := true
        ```
     2. Allow the prepaid segment $S$ to run to completion.
     3. At the segment boundary / safepoint, pause the execution context and yield control to the host with a `continuation_suspended` status.

### 3. Gas Consumption Invariant

Because the segment that triggers gas exhaustion is prepaid in full before executing, `gas_remaining` will become negative upon out-of-gas. The total gas consumed by an invocation is deterministically computed as:

$$\text{Gas Used} = \frac{\text{Gas Limit} - \text{Gas Remaining}}{10\,000}$$

> [!NOTE]
> When an execution traps due to running out of gas, `Gas Used` will legitimately exceed `gas_limit` by the fractional cost of the final, partially executed segment. This guarantees that the caller is never under-charged for work that the engine initiated.

---

## Host Environment & Embedder Interface

### Conceptual Embedder API

Conforming WebAssembly hosts provide the following functions to control and inspect execution metering:

```c
// Arms the runtime with a gas budget. Setting a limit of 0 or a negative value
// disables execution bounds or sets an empty budget.
void   wasm_set_gas_limit (wasm_runtime_t* runtime, double gas);

// Returns the initial gas limit assigned to the runtime.
double wasm_get_gas_limit (const wasm_runtime_t* runtime);

// Returns the exact amount of gas consumed by execution so far.
double wasm_get_gas_used  (const wasm_runtime_t* runtime);
```

### Trap Representation

- **Trap Message:** `out of gas`. Hosts that report traps as strings should use this message, so embedders can distinguish exhaustion from other traps.
- **Uncatchable:** An out-of-gas trap represents an external resource exhaustion condition. It **MUST NOT** be caught by WebAssembly exception handling blocks (`try_table`, `catch`, or `catch_all`). Control unwinds completely through the WebAssembly stack to the host invocation boundary.

### Command-Line Interface Conventions

Conforming CLI runtimes should support:
- `--gas-limit <limit>`: Sets a hard execution ceiling in gas units (e.g. `--gas-limit 1000.5`). When the budget is exhausted, the CLI exits with an out-of-gas error.
- `--gas-meter`: Enables gas accounting without imposing an early artificial termination ceiling (assigning a virtually boundless default budget, e.g. $10^{15}$ gas) and prints total gas usage upon completion:
  ```text
  Gas used: 124.5020
  ```

---

## Interactions with Advanced WebAssembly Features

### 1. Stack Switching and Continuations

In a runtime supporting typed continuations or stack switching:
- Gas remaining is a global store-level attribute shared across all continuations within the runtime instance. When child continuations execute, they spend from the same shared gas pool.
- When gas runs out in a suspendable runtime, instead of immediately destroying the active continuation with an uncatchable trap, the engine flags a suspension request. The active continuation yields gracefully at the boundary, allowing the host scheduler to reschedule, migrate, or cancel the task cleanly.

### 2. Process Snapshots and Resumption

Gas metering integrates cleanly with WebAssembly snapshots:
1. **Snapshot on Exhaustion:** A module running out of gas can pause at the boundary, capture its complete execution state, and return control to the host.
2. **Replenishing and Resuming:** The host can inspect intermediate state, grant an additional gas budget (`wasm_set_gas_limit`), and resume execution from the exact instruction where it paused.
3. **Resumption in Unmetered Runtimes:** A snapshot taken from a metered runtime carries pure abstract machine state (locals, operand stacks, globals, linear memory). It can be resumed without friction in an unmetered runtime or on an engine built without metering support.

### 3. Exception Handling

- Throwing an exception (`throw`, `throw_ref`) terminates the current execution segment. The search for a matching `try_table` handler proceeds without additional instruction execution within the throwing segment.
- Entering a catch handler begins a new execution segment and prepays for handler instructions.

### 4. Tail Calls

- Tail calls (`return_call`, `return_call_indirect`, `return_call_ref`) terminate straight-line segments.
- Because the tail call is priced as a branch and terminates the segment, an infinite tail-recursive loop repeatedly executes segment prepayment checks, ensuring it is strictly bounded by the gas limit.

---

## Implementation Notes and Design Considerations

This section documents non-normative architectural considerations and optimization strategies for engine implementers.

### 1. Compilation-Driven Segmentation vs. Interpreter Loop Checks

In an interpreter or JIT compiler, checking a gas counter on every virtual instruction introduces prohibitive overhead (often doubling execution time).

Decomposing code into straight-line segments dramatically reduces this overhead:
- The compiler aggregates all instruction costs within each basic block into a single integer constant during module compilation.
- At the start of the block, the engine emits a single synthetic charge operation carrying that constant.
- At runtime, metering overhead is reduced from $O(N)$ (where $N$ is total instructions executed) to $O(B)$ (where $B$ is the number of basic blocks entered). In a direct-threaded or register-based interpreter, this translates to one subtraction and one conditional branch per block.

### 2. Lazy Compilation Lifecycles

In engines featuring lazy compilation (compiling function bodies on first invocation):
- Runtimes should be armed with a gas budget **before** compiling function bodies. If a function is compiled while metering is active, the engine embeds the segment charge operations into the generated code.
- If a function was already compiled while metering was disabled, the engine must either invalidate/recompile the code upon arming, or require the embedder to set the gas limit prior to loading and instantiating the module.
- Re-arming an already metered runtime with a fresh budget simply updates `gas_remaining` in memory without requiring code invalidation.

### 3. Integer Arithmetic and Counter Sizing

- Engines should maintain the remaining gas counter as a signed 64-bit integer (`int64_t`).
- Using a 64-bit integer allows large budgets up to $\approx 9.22 \times 10^{14}$ gas ($2^{63}-1$ micro-units) without risking counter overflow.
- Individual segment costs easily fit within a 32-bit unsigned integer (`uint32_t`), allowing the cost immediate in compiled code pages to remain compact (4 bytes).

### 4. Prepayment and Safepoint Decoupling

When combining gas metering with asynchronous pause or snapshot capabilities:
- If a segment runs out of gas, immediately unwinding the stack inside the charge operation can leave the operand stack or local slots in an intermediate, engine-internal state.
- By allowing the prepaid segment to finish executing its instructions up to the next designated *safepoint* (e.g., loop back-edge or function call boundary), the engine guarantees that all live values on the stack conform to canonical validation types, simplifying stack capture and snapshot serialization.

---

## Implementation Status and Conformance

### Implementations

- **Wasm3** (interpreter): complete implementation of this specification, including suspendable execution and snapshot resumption.

### Conformance Tests

A conforming implementation is expected to pass the following scenarios:
1. **Bounded Tail Calls:** An infinite tail-call loop terminates deterministically with an *out of gas* trap under a constrained gas limit.
2. **Suspension and Resumption under Gas Limits:** A computation that reaches its gas limit suspends, serializes its state, is granted more gas, and resumes to the same result an uninterrupted run produces - including when the limit is reached inside deep recursion.
3. **Resumption Without Metering:** A state captured on gas exhaustion in a metered runtime resumes to correct completion in an unmetered one.
4. **Segmented Round Trips:** Running a module to completion in many gas-bounded legs yields the same observable result as a single run.


# WebAssembly Snapshot Proposal

**Title:** WebAssembly Process Snapshot, Checkpoint, and Resumption  
**Status:** Working Draft / Reference Specification  

---

## Abstract

This proposal specifies a standardized, host-agnostic binary format for capturing,
persisting, and resuming the complete execution state of a suspended WebAssembly
module instance.

A snapshot can be stored either as a **standalone container file** (magic `\0dmp`) or
**embedded directly into a WebAssembly binary module** as a standard custom section
(`"snapshot"` or `"snapshot.<name>"`). An embedded snapshot produces a valid,
self-contained WebAssembly binary that standard toolchains can process and compatible
engines can instantiate and immediately resume.

The format is defined strictly in terms of WebAssembly abstract machine state
(memories, tables, globals, segment statuses, continuation trees, and typed
activation frames), rendering it independent of host CPU architecture, pointer width,
value stack representation, endianness, or JIT/interpreter internal metacode layouts.

---

## Motivation and Use Cases

WebAssembly provides a sandboxed, deterministic virtual machine with a well-defined
memory and execution model. However, existing standards offer no uniform way to
checkpoint an active execution and resume it elsewhere.

### Primary Use Cases

1. **Serverless Fast-Start (Snapshot-to-Run):**
   Initializing large runtime environments (e.g., Python, Ruby, or JavaScript engines
   compiled to WebAssembly) incurs high startup latency. With snapshots, a module can
   execute its initialization phase once, capture a snapshot, and distribute the
   resulting module. Worker invocations can resume directly from the post-init state in
   sub-millisecond time.

2. **Live Process Migration across Heterogeneous Clouds & Edge Nodes:**
   A long-running computation started on an x86-64 server can be checkpointed,
   transferred over the network, and resumed seamlessly on an ARM64 or RISC-V edge
   device (or vice versa), without recompiling or losing execution context.

3. **Fault Tolerance and Distributed Checkpoint/Restart:**
   Scientific simulations, distributed workflows, and transactional workloads can
   checkpoint progress periodically. In the event of node failure or preemption,
   the computation restarts from the latest snapshot rather than from the beginning.

4. **Deterministic Time-Travel Debugging:**
   Capturing snapshots at key execution intervals allows developers to replay, inspect,
   and step through past program states deterministically.

5. **Stateful AI Agents and Long-Running Workflows:**
   Autonomous agents running complex multi-step reasoning can pause while waiting for
   external events, human feedback, or rate-limited API responses, serialize their entire
   execution state to durable storage, and resume upon receiving an event.

---

## Comparison with Prior Art

The WebAssembly Tool Conventions repository specifies a [Coredump format](https://github.com/WebAssembly/tool-conventions/blob/main/Coredump.md).
While both specifications deal with serialized execution state, their goals, semantics,
and technical capabilities differ fundamentally:

| Capability | WebAssembly Coredump | WebAssembly Snapshot (This Proposal) |
|---|---|---|
| **Primary Goal** | **Post-mortem debugging** (crash analysis) | **Execution resumption** (live restore & continue) |
| **Call Stack State** | Unwound debug traces, DWARF cross-references | **Live execution frames** with exact locals & operand stack |
| **Stack Switching** | Unsupported (single linear stack trace) | **Full continuation hierarchy** (Wasm Stack Switching) |
| **Exception Handling** | Trap info only | **Live `exnref` values and `try_table` frames** preserved |
| **Tables & Elements** | Untyped or omitted | **Full table instances & dropped segment tracking** |
| **Module Embedding** | Standalone dump file | **Embeddable as custom section (`"snapshot"`)** in valid `.wasm` |
| **Multi-Snapshot** | Single core dump | **Multiple named checkpoints** in a single binary |
| **Host Context** | Host state omitted | **Embedder hooks** for `externref` re-binding & host state |

`Coredump.md` serves as a crash dump for offline inspection via tools like `wasmgdb`.
This proposal defines an active, bidirectional serialization protocol capable of
re-materializing the running program on any conforming engine.

---

## Abstract Execution State Model

A WebAssembly process snapshot captures two layers of state:

### 1. Instance Store State
- **Linear Memories:** The current page count of each memory instance and its byte
  contents (omitting large runs of zeroed bytes).
- **Globals:** The runtime value of every mutable and immutable global variable.
- **Tables:** The runtime size of every table instance and the references it holds
  (`funcref`, `externref`, `exnref`, `contref`).
- **Segments:** The dynamic drop status of each data segment and element segment.

### 2. Execution / Activation State
- **Continuation Tree:** The root continuation of the paused invocation, plus all
  allocated, suspended, and resumed continuations reachable through locals, globals,
  or tables.
- **Activation Frames:** For each suspended continuation, the ordered chain of
  activation frames (from outermost caller down to the suspension point):
  - **Function Identifier:** Target function index within the module.
  - **Program Counter:** Byte offset of the instruction within the function's Wasm body.
  - **Suspension Site:** Nature of suspension (`loop`, `suspend`, `call`, `resume`,
    `entry`), and for a loop back edge, the loop it goes to.
  - **Values:** Canonical Wasm function locals $[L_0 \dots L_{M-1}]$ in declaration
    order, followed by every value on the operand stack $[S_0 \dots S_{K-1}]$, bottom
    first.
- **Exception Store:** All live exception packets (`exnref`) and their tag payloads.

### 3. The Host Boundary
State outside the WebAssembly abstract machine—such as open file descriptors, sockets,
thread IDs, and clocks—is explicitly not part of the Wasm core state. This proposal
defines structured embedder hooks:
- **`externref` Re-binding:** External references are serialized as symbolic 64-bit names
  defined by the embedder and rebound during restoration. A null reference has its own
  encoding, so `0xFFFF_FFFF_FFFF_FFFF` is reserved and an embedder must not use it as a
  name.
- **Host State Section:** An optional opaque section where the embedder can store its
  own serialized runtime context.

---

## Binary Format Specification

A snapshot is encoded using WebAssembly binary conventions:
- Numbers are encoded in **LEB128** format (unsigned `u32`/`u64`, signed `s32`/`s64`),
  except for the container's version and for float bit patterns, which are fixed width
  and little endian.
- Sequences are encoded as `vec(T) ::= count:u32, (elem:T)*`.
- Type identifiers correspond to standard Wasm `valtype` bytes, except `contref`
  (`0x68`), which is this format's own tag for the continuation-reference storage
  family: stack switching spells those types structurally and gives them no
  single-byte `valtype`.
- Content is organized into **Sections**, each with a 1-byte section ID and an LEB128
  payload length.

This version of the format targets a single module instance. Memories, tables and
globals an instance imports are captured as part of that instance, and restoring
writes back through the import to the cell the exporting instance owns; a snapshot
does not describe a link graph of several instances.

### Containers

A snapshot exists in one of two container forms:

#### A. Standalone Container (`.dmp`)
A standalone binary file starting with a 4-byte magic header and a 4-byte version:
```
magic:   0x00 0x64 0x6D 0x70  (\0dmp)
version: 0x01 0x00 0x00 0x00  (u32, little endian)
followed by: section*
```
The magic and the version are a fixed eight bytes, the way a WebAssembly binary opens,
so a reader can tell what it is holding before it knows anything else about the file.
All other integer fields wider than one byte are LEB128; float bit patterns
remain fixed-width little endian.

#### B. Embedded Custom Section (`.wasm`)
Embedded inside a standard `.wasm` module as a custom section:
```
section_id:  0x00 (Custom Section)
size:        u32 (LEB128 total section size)
name:        "snapshot" | "snapshot." <name>
payload:     section*
```
The payload starts directly with the Meta section: it has no `\0dmp` magic or
standalone version field. These custom-section names identify the version-one
section grammar defined here. A future incompatible embedded grammar must use
another section name.

An embedded snapshot is there to be resumed, so it **MUST** be resumable: a producer
**MUST NOT** embed a postmortem, and a reader rejects one. Otherwise a module carrying
a postmortem as `"snapshot"` could no longer be run at all, since running it resumes
that section. For the same reason it **MUST** belong to the module that carries it: a
producer **MUST NOT** embed a snapshot whose `wasm_hash` differs from the enclosing
module's.

When embedded, the enclosing `.wasm` file remains a valid, standard WebAssembly module.

##### Section Naming and Multi-Snapshot Convention
- **Default Snapshot (`"snapshot"`):** Designated as the primary / default execution
  checkpoint of the module. A module **MAY** contain at most one section named `"snapshot"`.
- **Named Snapshots (`"snapshot.<name>"`):** Represents a specific named checkpoint
  where `<name>` is a non-empty UTF-8 identifier (e.g. `"snapshot.checkpoint1"`,
  `"snapshot.init"`). A module **MAY** contain multiple named snapshot sections,
  but each complete section name **MUST** be unique. Embedding a checkpoint under an
  existing name replaces that section.
- **Duplicate Names:** Two sections with the same complete name do not make the module
  invalid - a custom section never does. A host asked to select that name **MUST**
  refuse it rather than pick one of the two. Other names, and a cold start, are
  unaffected.

##### Host Instantiation and Resumption Semantics
When a runtime supporting this specification instantiates a WebAssembly module containing
embedded snapshots:
1. **Default Resumption:** If a section named `"snapshot"` is present, the runtime **SHOULD**
   automatically resume execution from this snapshot by default.
2. **Cold Start Override:** The host **MUST** provide a mechanism (e.g. `--resume none`) to
   bypass resumption and execute a cold start from `_start` or the module's entry point.
3. **Named Selection:** The host **SHOULD** support selecting a specific named snapshot via
   command-line or API syntax, such as `<file.wasm>:<name>` or `--resume <file.wasm>:<name>`.
   Such an argument splits at its last `.wasm:`, so a directory whose name contains one
   does not end the path early.
4. **Start Function:** When resuming a snapshot, the module's `start` function **MUST NOT**
   be re-executed, as its store modifications are already reflected in the snapshot.
5. **Explicit Export Invocations:** If the host is asked to invoke an exported function
   other than the module's default entry point (e.g. `--func <name>`), the runtime
   **SHOULD** bypass snapshot resumption and execute the targeted function. Naming the
   default entry point is not such a request: a host that cannot tell `--func _start`
   from its own default may resume in both cases.
6. **One Module:** An embedded snapshot restores into the module that carries it and
   into no other. Selecting one by its module (e.g. `--resume <file.wasm>`) names the
   module to run as well, so the host **MUST NOT** accept a second module to restore it
   into. A checkpoint wanted in another binary - one that differs only in its custom
   sections, say - is extracted into a standalone container first, and restored from
   there under the usual `wasm_hash` check.

---

## Section Grammar

The following sections may appear within a snapshot container. Unrecognized sections
can be skipped using the section length.

The Meta section comes first; nothing may precede it, because it says what the restoring
engine has to make ready. Sections after Meta may appear in any order, and a reader must
accept any order: a reference can name something a later section defines, so the
cross-record constraints below are settled once every section has been read. Producers
**SHOULD** emit them in ascending ID order. No section may appear twice, and a section
whose body does not consume exactly its declared length makes the snapshot malformed.

A section is present exactly when it has something to say. One with nothing to say is
left out rather than written empty, and a reader rejects one that is there anyway -
a zero-length body, or a vector that lists nothing:

- **Meta** is always present.
- **Memory**, **Table** and **Global** are each present exactly when the module has at
  least one memory, table or global respectively, and each lists every one of them -
  there is no "unchanged since instantiation" encoding.
- **Segment** is present exactly when the module has at least one data or element
  segment, and lists both kinds.
- **Exception** is present exactly when `num_exceptions` is not zero.
- **Continuation** is present in every resumable snapshot, and in a postmortem exactly
  when `num_continuations` is not zero.
- **Host State** is present exactly when the embedder has bytes to carry.

| ID | Section Name | Description |
|---|---|---|
| `0` | **Meta** | Header information: timestamp, flags, module hash |
| `1` | **Memory** | Linear memory page counts and sparse chunk streams |
| `2` | **Table** | Table sizes and reference elements |
| `3` | **Global** | Current values of global variables |
| `4` | **Segment** | Dynamic drop status of data and element segments |
| `5` | **Exception** | Active exception instances and tag payloads |
| `6` | **Continuation**| Call stacks, frames, continuations, and live value stacks |
| `7` | **Host State** | Optional opaque embedder data |

---

### Section 0: Meta Section

Contains snapshot metadata and compatibility checks:

```
meta_section ::= 
    flags:             u32            (0x1 = postmortem/non-resumable dump, 0x0 = resumable)
    timestamp_ms:      u64            (UTC milliseconds since Unix epoch; 0 if unavailable)
    wasm_hash:         u64            (hash of the module, as defined below)
    num_continuations: u32            (Total continuations stored; index 0 is root in resumable snapshots)
    num_exceptions:    u32            (Total exceptions stored)
    exception_tags:    u32[num_exceptions] (Tag index for each exception)
```

A snapshot carries no name of its own. An embedded one is named by its custom section
and selected by that name; a standalone `.dmp` is named by its file. A label repeated
inside Meta would be a second answer to a question the container already answers, and
the two could disagree.

Only bit `0x1` of `flags` is defined. Every other bit is reserved, and a reader
**MUST** reject a snapshot that sets one rather than ignoring it: a future flag will
change what the rest of the file means, and a reader that skipped it would restore a
state that was never captured.

#### Module identity

`wasm_hash` is [XXH64](https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md)
with seed `0`. The input is the concatenation of every **non-custom section**
(`section_id != 0`) in its original order in the module. Include each section's ID
byte, its original LEB128 length bytes, and its complete payload. Exclude the module's eight-byte
magic/version header and all custom sections, including names, debug information,
and embedded snapshots. Thus changing custom metadata does not invalidate a snapshot,
but changing a standard section or its binary encoding does.

An engine that cannot compute the hash - it no longer holds the module's bytes -
**MUST** refuse to write or restore a snapshot rather than write or accept some
stand-in value.

### Section 1: Memory Section

Serializes the memory instances of the module:

```
memory_section ::= vec(memory_instance)

memory_instance ::= 
    mem_index:     u32           (Index of the memory in the module)
    current_pages: u32           (Current allocation size, in the pages the memory declares)
    chunks:        chunk*        (Sparse stream of non-zero regions, terminated)

chunk ::= 
    kind:          u8            (0x00 = end, 0x01 = raw bytes, 0x02 = fill 0xFF)
    offset:        u32           (Byte offset in linear memory; absent when kind == 0x00)
    length:        u32           (Byte length of this chunk; absent when kind == 0x00)
    data:          bytes[length] (Only present when kind == 0x01)
```
The chunk stream is terminated rather than counted: a chunk of kind `0x00` ends it and
carries neither offset nor length. Every memory listed carries a stream, and a memory the
restoring engine does not hold carries an empty one, so a reader can always walk past it.

Memories are listed once each, in index order, so `mem_index` is a check on the stream
rather than a selector: record *i* **MUST** carry `mem_index == i`, and a reader
**MUST** reject a stream that repeats or skips one. The same holds for `table_index`
and `global_index` in the two sections that follow.

A memory may not shrink across a snapshot: `current_pages` must be at least the page
count the module instantiates with, and no more than its declared maximum.

Chunks are applied in the order they appear, over pages the loader has already zeroed.
A chunk must lie inside `current_pages` worth of bytes; a zero-length chunk is
permitted and does nothing. Chunks **SHOULD** be disjoint and ascending, and a reader
that applies them in order needs no more than that - where two overlap, the later one
wins.

*Note: which runs a producer omits is its own choice, and a reader must accept any
chunking of the same bytes. The reference implementation drops runs of `0x00` and emits
a fill chunk for runs of `0xFF` once they reach a build-time threshold, 128 bytes by
default.*

A page is whatever size the memory declares, not always 64 KiB, and the file does not
repeat it: both ends read it from the same module. What the file does fix is the width -
`current_pages`, `offset` and `length` are `u32` - so this version describes memories
below 4 GiB, however that is divided into pages. A producer whose memory is larger
**MUST** refuse to write the snapshot rather than truncate; carrying one needs a later
version.

### Section 2: Table Section

Serializes the contents of tables:

```
table_section ::= vec(table_instance)

table_instance ::= 
    table_index:  u32            (Index of the table in the module)
    elem_type:    valtype        (funcref: 0x70, externref: 0x6F, exnref: 0x69, contref: 0x68)
    elements:     vec(ref_value) (Current element reference IDs)

ref_value ::= 
    kind:         u8             (0x00 = null, 0x01 = func_idx, 0x02 = extern_id, 0x03 = exn_id, 0x04 = cont_id)
    id:           u64            (Identifier or index; omitted if null)
```

### Section 3: Global Section

Serializes the values of all globals:

```
global_section ::= vec(global_instance)

global_instance ::= 
    global_index: u32            (Index of the global in the module)
    val:          typed_value    (Current value)
```

### Section 4: Segment Section

Tracks dynamic segment drops caused by `data.drop` or `elem.drop`:

```
segment_section ::= 
    data_dropped: vec(u8)        (1 if data segment i has been dropped, else 0)
    elem_dropped: vec(u8)        (1 if element segment i has been dropped, else 0)
```

### Section 5: Exception Section

Serializes active exception instances held by `exnref` variables:

```
exception_section ::= vec(exception_instance)

exception_instance ::= 
    exception_id: u32            (Unique identifier for this exception instance)
    args:         vec(typed_value) (Payload arguments matching tag signature)
```

The tag for exception `i` is stored only in `Meta.exception_tags[i]`. Exception
records appear in ascending ID order, from zero through `num_exceptions - 1`,
and their argument types and counts must match that tag's signature.

### Section 6: Continuation Section

Serializes the complete call tree and all continuation objects (including the root
continuation representing the paused invocation):

```
continuation_section ::= vec(continuation_instance)

continuation_instance ::= 
    cont_id:        u32          (Unique identifier; 0 is the root in resumable snapshots)
    state:          u8           (0 = allocated, 1 = suspended, 2 = finished)
    is_root:        u8           (1 for the paused invocation itself, else 0)
    type_index:     u32          (Index of its `cont $ft` type; 0xFFFFFFFF for the root)
    entry_func_idx: u32          (Function this continuation was created with; for the root, the function invoked)
    resume_throw:   ref_value    (Exception delivered but not yet raised, or null; see below)
    bound_count:    u32          (Absent in postmortems. Number of arguments already bound; zero for root/finished)
    bound_args:     typed_value[bound_count] (Only state 0; types come from the entry function's original parameter prefix)
    activations:    vec(activation)  (Absent in postmortems. Only when state == 1: the stack of active
                                      function frames, outermost first)

activation ::= 
    func_index:     u32          (Function executing in this frame)
    site_kind:      u8           (0 = loop, 1 = suspend, 2 = call, 3 = resume, 4 = entry)
    wasm_offset:    u32          (Offset of the instruction in the function's code entry; see below)
    target_loop:    u32          (Only present when site_kind == 0: offset of the loop the back edge goes to)
    values:         vec(typed_value) (Locals [0..M-1] followed by the operand stack [0..K-1], bottom first)
    target_cont:    ref_value    (Only present when site_kind == 3 (resume): non-null, non-root continuation being resumed)
```

Every `wasm_offset` counts bytes from the start of the function's entry in the code
section, just past that entry's size field. Offset 0 is therefore the count of local
declaration groups, and the first instruction sits after the local declarations. That
is where the code entry's own framing ends, so the offset does not depend on how the
size was encoded or where the function sits in the module.

A back edge is identified by the instruction that branches and the loop it branches
to. `wasm_offset` names the instruction: the branch itself for `br`, `br_if`,
`br_table` and the `br_on_*` family, the `try_table` for a branch taken by one of its
catch clauses, and the `resume` or `resume_throw` for a branch taken by one of its `on`
clauses. `target_loop` is the `wasm_offset` of the `loop` opcode the branch's label
names. One instruction can branch back to several loops - a `br_table`, or a
`try_table` with several catch clauses - and each target leaves a different frame. Two
labels or clauses of one instruction that name the same loop leave the same frame, so
they share one identity, and a reader may use any of its own safepoints that matches
it. A reader **MUST** reject a back edge whose `target_loop` does not name a `loop` in
the function, names one that does not enclose the instruction, or names one the
instruction cannot branch to.

A frame does not list the `loop` and `try_table` regions around it: they are fixed by
where it stands, so a reader works them out from the module. They are the regions
that hold the safepoint's offset, from the opening instruction to its `end`, both
ends excluded. A catch clause's safepoint is at its own `try_table`, so that
`try_table` is not among them - its handler runs once the region is done. A back edge
has retired the handlers of the `try_table`s it leaves: those inside its target loop.

#### What a frame holds

`values` holds the function's locals, in declaration order, then every value on the
operand stack at the safepoint, bottom first, as the Wasm validator sees that stack. A
block's parameters are ordinary values on it. Which values that is depends on the kind:

| Kind | Stack in the frame |
|---|---|
| `0` back edge | The stack as it was when the target loop was entered, then the loop's parameters, which the branch has just written. Values pushed inside the loop are dropped, and so is a `br_if` condition or a `br_table` index. |
| `1` suspend | The stack just after the `suspend` or `switch`, its results included. |
| `2` call | The stack below the call's operands. The arguments are consumed, and the results are not there yet. |
| `3` resume | The stack below the operands of `resume`, `resume_throw` or `resume_throw_ref`. The arguments, the continuation reference and any exception are consumed. |
| `4` entry | Empty. The locals hold the arguments and each declared local's default value. |

The [example frames](../../test/snapshot/frames/README.md) write this out for one
module per case, for an engine to check its own output against.

#### Tail calls

A `return_call`, `return_call_indirect` or `return_call_ref` replaces the frame that
makes it, so no activation is ever stopped at one. An engine that runs one as a call
and a return leaves the waiting caller out of the snapshot: the caller only passes
its callee's results on, and the callee returns the caller's result types, so nothing
is lost with it.

A reader places the callee's frame where the left-out caller's frame began - at the
continuation's base, or where the call outside it says its callee starts - and when
the callee returns, its results land where the caller's would have. There is nothing
to rebuild.

So the function in a frame need not be the one the call outside it named, and the
outermost function need not be `entry_func_idx`. What a reader checks instead is what
they return. The outermost activation's function returns what the continuation
returns: its entry function's results, each a subtype of the entry function's. The
function of an activation below a `call` safepoint returns what that call expects,
each result a subtype of the called type's.

#### Postmortem snapshots

The postmortem flag makes a snapshot non-resumable. Meta and store sections use
the same grammar as resumable snapshots. There is no root invocation:
`num_continuations` may be zero, in which case the Continuation section is left out,
and every stored continuation has `is_root = 0`.
A running continuation is recorded as finished. Continuation records stop immediately
after `resume_throw`: **neither the bound-argument count nor activations are present**,
regardless of state. Only continuation objects reachable from the recorded store
are included; the active invocation's frames are not captured. Host State is absent.
Readers may inspect these snapshots but must refuse to resume them.
The refusal belongs after Meta has been read whole, not at the flag: the fields that
follow the flag are present and mean what they always mean, and a tool that stops
mid-section cannot then walk the rest of the container.

#### Bound continuations

`type_index` names the continuation's **current** type, after any `cont.bind`
operations. Binding consumes a prefix of its parameters; repeated bindings accumulate
in `bound_count`. For an allocated continuation, `bound_args` holds this entire prefix
in the entry function's original parameter order and types. The current type must
match the remaining parameter suffix and the entry function's results. A fully bound
continuation has no remaining parameters but still stores all bound values.

For a suspended continuation, `bound_count` counts results of its pending `suspend`
or `switch` that have already been supplied by binding. Their values are stored in
the corresponding leading result slots of the innermost suspension's `values` vector;
there is no separate `bound_args` payload. Follow `target_cont` through any nested
resume activations to find that suspension. The current type's parameters must match
the unbound result suffix. Unfilled result slots use zero/null placeholders, including
null for a non-nullable reference; bound slots must satisfy their complete types.
Once execution resumes, the binding count resets to zero. A continuation already
executing under a saved resume has received its arguments and has a zero binding count.

#### The root and the entry function

In a resumable snapshot, the root is the record with `cont_id` 0, and it is the only
one with `is_root = 1`. `is_root` repeats what the ID already says. It stays in the
format as a check, and a reader rejects a record where the two disagree. The root:

- has state 1 (suspended),
- has `type_index` 0xFFFFFFFF, because the paused invocation was called by the host
  and has no continuation type,
- names the function the host invoked as its `entry_func_idx`,
- has `bound_count` 0 and a null `resume_throw`.

Every continuation, the root included, was made with a function, so `entry_func_idx`
always names one; 0xFFFFFFFF is not a valid value there. After a tail call it need not
be the function of the first activation (see [Tail calls](#tail-calls)).

#### Pending exceptions

`resume_throw` is an exception that `resume_throw` or `resume_throw_ref` has handed to
the continuation but that has not been raised yet. It must be an exception reference:
kind `0x00` (null) or `0x03` (`exn_id`). For a suspended continuation, it is
raised at the innermost suspension point as soon as execution continues, before
anything else runs there. For an allocated one, it is raised as soon as the
continuation is resumed, without running its function. A finished continuation and
the root always carry null (`0x00`).

#### Resume links

Resume links must form acyclic chains of suspended continuations and cannot target
the root. Wasm continuation references cannot name the root invocation either. A
continuation runs under one resume at a time, so no two `resume` activations - in the
same continuation or in different ones - may target the same continuation. A reader
**MUST** reject a snapshot where they do.

A reference elsewhere - in a local, a global, a table or an exception payload - may
still name a continuation that a saved resume is running. Wasm does not clear the
reference a `resume` consumed. That reference names the same record, whose state is 1
because the chain it belongs to is suspended. It is not a second owner. Once execution
continues, that continuation is running under its resume again, and resuming, binding
or switching to it through the other reference fails exactly as it would have had the
program never been saved.

#### Safepoint Semantics and Kinds

A function in a suspended continuation is stopped at a **safepoint**. Every engine has
the same ones, since each is fixed by an instruction of the module:

| Kind | Name | Where |
|---|---|---|
| `0` | `loop` / back edge | Every branch to a `loop` label: `br`, `br_if`, `br_table`, the `br_on_*` family. A catch clause's is at its `try_table`, and an `on` clause's at its `resume` or `resume_throw`. |
| `1` | `suspend` | Immediately past a `suspend` or `switch` |
| `2` | `call` | A `call`, `call_indirect` or `call_ref` waiting on its callee - never a `return_call*` |
| `3` | `resume` | A `resume`, `resume_throw` or `resume_throw_ref` waiting on the continuation it runs |
| `4` | `entry` | Before the first instruction of a function body. Its `wasm_offset` is that instruction's, just past the local declarations - the body's `end` when the body is empty. |

A pause starts only at kinds `0`, `1` and `4`. Kinds `2` and `3` are where an outer
frame waits while something deeper is paused.

A pause the host requests takes effect at the next kind `0` or `4` safepoint. How long
that takes is up to the engine, but it is bounded: every cycle a program can run passes
one of them - a loop its back edge, recursion a function's entry. A host import that
is still running must return first.

Resuming from a kind `0` or `4` safepoint goes past it: a pause request that is already
set when execution continues takes effect at the next safepoint, not at the one just
resumed from. Otherwise a request made before resuming would pause again on the spot,
with no progress.

In each suspended continuation's activation stack:
- `activations` is not empty.
- All outer (non-innermost) frames represent outgoing calls waiting on a callee and
  **MUST** have `site_kind == 2` (`call`).
- The innermost frame represents the suspension point and **MUST NOT** have
  `site_kind == 2` (`call`); its kind must be one of `0` (`loop`), `1` (`suspend`),
  `3` (`resume`), or `4` (`entry`).
- A `resume` activation (`site_kind == 3`) is always innermost within its continuation,
  and its `target_cont` identifies the child continuation it runs.


### Value Representation: `typed_value`

Values are encoded self-describing using standard Wasm `valtype` tags:

```
typed_value ::= 
    type: valtype
    case type of:
        0x7F (i32)       => s32 (LEB128)
        0x7E (i64)       => s64 (LEB128)
        0x7D (f32)       => 4 bytes (IEEE 754 Little-Endian)
        0x7C (f64)       => 8 bytes (IEEE 754 Little-Endian)
        0x7B (v128)      => 16 bytes (raw vector bytes)
        0x70 (funcref)   => ref_value
        0x6F (externref) => ref_value
        0x69 (exnref)    => ref_value
        0x68 (contref)   => ref_value
```

A `v128` is a vector of bytes and has no byte order of its own, so it travels verbatim.

The reference tag identifies its storage family, not its complete type. A loader
must check the payload's reference kind against that tag, then validate the value
against the destination's full Wasm type: nullability and, for typed function or
continuation references, heap-type compatibility. Use module declarations for globals,
tables, exception payloads and bound arguments, and the safepoint's types for frame
values. A function index with a different signature is invalid even if it is in range.
Forward continuation references must be checked after their type records are loaded,
before execution can resume. An uninitialized non-defaultable local is also represented
by null; a loader permits that placeholder only where the local is not definitely
initialized in the instruction's validation context. Once initialized, its declared
nullability applies. Unfilled suspension result slots have the separate placeholder
exception described above.

### Section 7: Host State Section

Embedder-specific extension payload:

```
host_state_section ::= 
    data: bytes                  (Opaque host data: the whole section body)
```
The section's own length is the payload's length, and the section is left out when
the embedder has nothing to carry. The embedder must read back exactly that many
bytes. Reading fewer or more makes the snapshot malformed, so a host that changes what
it stores cannot silently desync the sections that follow.

---

## Restoration and Re-Materialisation

Restoring a snapshot does not require identical machine code or memory addresses.
A conforming runtime performs the following steps:


1. **Instantiation:** Parse and instantiate the original module, and run none of its
   code - not even its `start` function. A snapshot restores only into such a freshly
   instantiated instance: a reader **MUST** refuse one that has run anything, rather
   than overwrite a store whose history it cannot see. The bounds below are measured
   against that fresh instance.
2. **Verification:** Confirm that `wasm_hash` matches the module's non-custom sections
   using the algorithm above, and that the host runtime supports all required features
   (e.g. floats, multi-memory).
3. **Store Restoration:** Recreate memories (pre-zeroing pages and applying chunks),
   restore global values, repopulate table references, and mark dropped segments.
4. **Function Safepoint Mapping:** For each activation frame in the snapshot, locate the
   target function and binary-search its compiled safepoints using
   `(wasm_offset, site_kind)`, and for a back edge, `target_loop`: one instruction can
   branch back to several loops, and each leaves a different frame. The `loop` and
   `try_table` regions around the frame come from the module, not the snapshot.
5. **Value Scatter:** Using the destination engine's safepoint map, place canonical locals
   and operand stack values into the engine's physical stack slots and registers. A
   frame goes where the call outside it says its callee starts, even when a tail call
   has replaced that callee.
6. **Continuation Replay:** Reconstruct native frames and resume execution.

---

## JavaScript Host API

Conforming WebAssembly host environments that implement the W3C WebAssembly JavaScript
Interface provide native bindings for snapshot capture, inspection, and resumption
under the `WebAssembly` namespace.

### WebIDL Specification

```webidl
[Exposed=(Window,Worker,Worklet)]
namespace WebAssembly {

  [Exposed=(Window,Worker)]
  interface Snapshot {
    /* Construct from standalone container bytes or extracted section */
    constructor(BufferSource bytes);

    /* Metadata Inspection */
    readonly attribute boolean resumable;
    readonly attribute bigint wasmHash;
    readonly attribute bigint timestamp;
    readonly attribute unsigned long continuationsCount;
    readonly attribute unsigned long exceptionsCount;
    readonly attribute ArrayBuffer? hostState;

    /* Export as standalone container (.dmp binary) */
    ArrayBuffer toArrayBuffer();

    /* Embedded snapshot helpers */
    static sequence<DOMString> getNames(Module module);
    static Snapshot? fromModule(Module module, optional DOMString name = "");
    static ArrayBuffer embed(BufferSource wasmBytes, Snapshot snapshot, optional DOMString name = "");
    static ArrayBuffer extract(BufferSource wasmBytes, optional DOMString name = "");
  };

  /* Callbacks for Host Boundary */
  callback ExternRefNamer = bigint (any externref);
  callback ExternRefBinder = any (bigint name);
  callback HostStateLoader = void (ArrayBuffer hostState);

  /* Capture options for active instances */
  dictionary SnapshotOptions {
    boolean postmortem = false;
    BufferSource hostState;
    ExternRefNamer nameExternRef;
  };

  dictionary SnapshotModuleOptions : SnapshotOptions {
    DOMString name = "";
  };

  /* Restoration options passed to instantiate / new Instance */
  dictionary SnapshotRestoreOptions {
    (Snapshot or BufferSource) snapshot;
    DOMString resumeName;
    boolean coldStart = false;
    ExternRefBinder bindExternRef;
    HostStateLoader loadHostState;
  };

  /* Instance Extensions */
  partial interface Instance {
    Snapshot takeSnapshot(optional SnapshotOptions options = {});
    ArrayBuffer takeSnapshotModule(BufferSource wasmBytes, optional SnapshotModuleOptions options = {});

    void requestSuspend();
    readonly attribute boolean isSuspended;
    any resume();
  };
};
```

### API Semantics and Lifecycle

#### 1. Snapshot Construction and Inspection (`WebAssembly.Snapshot`)

- `new WebAssembly.Snapshot(bytes)` parses the provided `BufferSource`. It accepts
  either a standalone container (beginning with `\0dmp` magic and version `1`) or an
  extracted custom section payload (starting directly with the Meta section). The
  constructor validates the header and Meta section; if the container is malformed or
  uses unsupported flags, it throws a `WebAssembly.CompileError`.
- `resumable` reflects bit `0x1` of `flags` (`true` for resumable snapshots, `false`
  for postmortem dumps).
- `wasmHash` is an unsigned 64-bit integer (`bigint`) representing the XXH64 hash of
  the originating module's non-custom sections.
- `toArrayBuffer()` serializes the snapshot into the standalone `.dmp` container
  format, including the 8-byte magic and version header.
- Static helper `Snapshot.getNames(module)` inspects custom sections in `module` and
  returns an array of snapshot names present (empty string `""` for the default
  `"snapshot"` section, and `<name>` for `"snapshot.<name>"`).
- Static helper `Snapshot.embed(wasmBytes, snapshot, name)` embeds `snapshot` into
  `wasmBytes` as a custom section, replacing any existing section of the same name.
  It throws a `TypeError` if `snapshot.resumable` is `false` or if `snapshot.wasmHash`
  does not match `wasmBytes`.

#### 2. Capturing Execution State (`instance.takeSnapshot`)

A snapshot can be captured whenever an instance is paused at a valid safepoint:
- **Inside a Host Callback:** When WebAssembly invokes an imported JavaScript function,
  the instance is stopped at a `call` safepoint. The host callback can invoke
  `instance.takeSnapshot()`.
- **After Cooperative Pause:** Calling `instance.requestSuspend()` requests execution to
  yield at the next loop back-edge or function entry safepoint. Once paused,
  `instance.isSuspended` returns `true`, and `instance.takeSnapshot()` captures the
  exact live state.
- **Postmortem:** If `options.postmortem: true` is set, a non-resumable store dump is
  produced without active continuation frames.

If `options.nameExternRef` is provided, it is invoked for each non-null `externref` in
the store and stack, mapping it to a 64-bit integer (`bigint`). If a non-null
`externref` is encountered and no namer is provided (or it returns
`0xFFFF_FFFF_FFFF_FFFFn`), `takeSnapshot` throws a `TypeError`.

#### 3. Restoration and Resumption (`WebAssembly.instantiate`)

Restoration integrates directly into standard instantiation via `SnapshotRestoreOptions`:

1. **Start Function:** When an instance is restored from a snapshot, the module's
   declared `start` function **MUST NOT** be executed.
2. **Hash Verification:** The runtime verifies that `snapshot.wasmHash` matches the
   instantiated module. Mismatches throw a `WebAssembly.LinkError`.
3. **`externref` Re-binding:** For every stored reference, `options.bindExternRef(id)`
   is invoked to restore the JavaScript host object reference. If a snapshot holds
   non-null `externref` values and `bindExternRef` is omitted, instantiation throws a
   `WebAssembly.LinkError`.
4. **Host State Callback:** If Section 7 is present and `options.loadHostState` is
   supplied, it is called with an `ArrayBuffer` containing the raw host payload.
5. **Execution Resumption:** Upon successful instantiation with a snapshot, the instance
   is in a suspended state (`instance.isSuspended === true`). Calling `instance.resume()`
   resumes execution from the recorded safepoint, returning the result of the original
   invocation.

### Usage Examples

#### Snapshot-to-Run (Pre-initialized Serverless Fast-Start)

```javascript
// Pre-initialization phase:
const baseBytes = await fetch("app.wasm").then(r => r.arrayBuffer());
const module = await WebAssembly.compile(baseBytes);
const initInstance = new WebAssembly.Instance(module, imports);

// Run complex startup sequence:
initInstance.exports.init();

// Embed live state into binary as named checkpoint:
const prewarmedWasm = initInstance.takeSnapshotModule(baseBytes, { name: "ready" });

// Worker execution phase:
const { instance } = await WebAssembly.instantiate(prewarmedWasm, imports, {
  resumeName: "ready"
});
const response = instance.resume();
```

#### Cross-Host Process Migration with External References

```javascript
// On Sender (Host A):
const snapshot = instance.takeSnapshot({
  nameExternRef: (obj) => BigInt(obj.channelId),
  hostState: new TextEncoder().encode(JSON.stringify({ step: "dispatch" }))
});
await sendOverNetwork(snapshot.toArrayBuffer());

// On Receiver (Host B):
const buffer = await receiveFromNetwork();
const snapshot = new WebAssembly.Snapshot(buffer);

const { instance } = await WebAssembly.instantiate(module, imports, {
  snapshot,
  bindExternRef: (id) => findActiveChannel(id),
  loadHostState: (raw) => restoreHostState(JSON.parse(new TextDecoder().decode(raw)))
});

// Resume execution seamlessly:
const result = instance.resume();
```

---

## Security and Soundness Considerations

1. **Validation Invariants:** A runtime must validate all snapshot bounds before applying
   them:
   - Memory page counts must not exceed declared maximums, and must not fall below the
     count the module instantiates with.
   - Table sizes must not exceed table limits.
   - Instruction offsets (`wasm_offset`) must point to valid safepoints within the
     function code.
   - Types of restored locals and stack values must strictly match the types the
     validator gives them at that safepoint, as [What a frame holds](#what-a-frame-holds)
     lists them.
   - A back edge's `target_loop` must name a `loop` that encloses the instruction and
     that the instruction branches to.
   - The outermost activation's function must return what its continuation returns,
     and each function below a `call` safepoint what that call expects.
   - Memories, tables and globals must be listed once each, in index order, with the
     count the module declares.
   - Undefined `flags` bits, zero-length sections, sections present with nothing to
     say, repeated sections and a section whose body does not consume its declared
     length are all malformed.
   - Non-innermost activation frames must be `call` (2) sites, the innermost frame must
     be a suspension site (0, 1, 3, or 4), and `activations` must not be empty.
   - No two `resume` activations may target the same continuation.
   - A non-root continuation's `type_index` must name a continuation type; the root
     must be suspended and carry no continuation type, no bound arguments and a null
     `resume_throw`. Every `entry_func_idx` must name a function.
   A reader that cannot enforce one of these must refuse the snapshot rather than
   restore part of it: a half-applied store is a state the program was never in.
   A restore that fails after it has begun changing the instance - including on a
   check that can only be made once every section is read - leaves that instance
   **unusable**. The engine **MUST** refuse to run it, resume it, snapshot it or restore
   into it again, and the embedder must discard it.
2. **Isolation & Capabilities:** Snapshots cannot forge host capabilities. An `externref`
   is never deserialized as a raw pointer; it must pass through the embedder's binding
   callback (`bindExternRef`). If an embedder provides no binding callback, snapshots
   containing non-null `externref` values are rejected.
3. **No incidental state:** A snapshot carries live values and nothing around them.
   Floating-point bit patterns are carried verbatim, NaN payloads included - normalizing
   them would change what the program goes on to compute, so they are state, not noise.
   What must not reach the file is everything the abstract machine cannot see: slots the
   safepoint does not list, padding between them, and whatever the engine's own frame
   happens to hold. A producer emits exactly the values a safepoint names, so a snapshot
   of the same execution is the same bytes on any engine, and carries nothing from the
   process that wrote it.

---

## Tooling and Reference Implementation

- **Wasm3 Engine:** Reference C implementation supporting `--snapshot` and
  `--resume` on standalone `.dmp` and embedded `.wasm` modules.
- **`snapshot-tool.py`:** Independent reference Python utility capable of:
  - Inspecting snapshots (`info`).
  - Verifying structural integrity and round-trip equality (`verify`).
  - Unpacking to human-readable JSON + raw binary blobs (`unpack`).
  - Repacking into byte-identical containers (`pack`).
  - Embedding into or extracting from `.wasm` binaries (`embed` / `extract`).

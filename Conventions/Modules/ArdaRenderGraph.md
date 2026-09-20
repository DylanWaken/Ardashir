# ArdaRenderGraph Coding Conventions

These conventions apply to project-owned C++ types in `Source/ArdaInfra/ArdaDependencyGraph` and supplement the [repository conventions](../ArdaCodingConventions.md).

## Public names

Use the project-wide `Arda` stem with the applicable category prefix: `FArda` for classes and structs, `IArda` for interfaces, `EArda` for enums and `TArda` for templates. Examples include `FArdaDependencyGraph`, `FArdaInductor`, `EArdaDependencyAccess` and `TArdaDependencyNodeDefinition<Parameters>`. Public APIs describe persistent graph concepts; native recording implementation remains private.

## Node authoring

Make graph programs read as operations on resource variables. Begin an `FArdaGraphEdit`, obtain `FArdaGraphBuffer` / `FArdaGraphTexture` values through allocation, binding or earlier operations, then call operation functions or a node's global callable such as `CopyBuffer(Edit, Parameters)`. Retain terminal resources with `Edit.Keep()`, check `Edit.Commit()` to queue the edit, and check `Graph.Compile()` before execution. Use the default `AllocateBuffer`, `AllocateTexture`, `BindBuffer`, `BindTexture`, `Clear`, `Upload`, `CopyTo`, `Clone` and `Readback` operations where they apply; programs should not need to construct node names or discover raw output handles for ordinary composition.

Give every node class its own parameter struct, including nodes with identical fields or no inputs. Do not reuse another node's parameter type, alias it, or pass a shared catch-all struct to multiple node classes. Include only that operation's inputs and outputs and use named fields instead of positional resource arrays. Shared immutable data may be referenced explicitly, while deliberately dynamic pointees need a documented synchronization contract. Each attached node instance retains a separate immutable parameter snapshot; edits to a caller's struct do not update attached nodes.

Publish every reusable node with `ARDA_DECLARE_GRAPH_NODE(FArdaMyNode, RunMyOperation);` immediately after its class declaration. Define the same pair once with `ARDA_DEFINE_GRAPH_NODE(FArdaMyNode, RunMyOperation);` in the owning implementation, in the same namespace. The named global const object is shared across translation units and retains no graph or execution state. Choose a callable name unique across `arda`; keep its C++ identity separate from the process-wide registry name in `GetMetadata`. The macros perform no startup registration or device setup. Direct calls register on demand; explicit `FArdaMyNode::Register()` supports discovery. See the [complete registration walkthrough](../../Docs/ArdaRDG/Registering-Nodes.md).

Use the exported callable for direct composition: `RunMyOperation(Edit, Parameters)` or `RunMyOperation(Edit, Input, Output, Value)`. Positional arguments aggregate-initialize `FArdaParameters` in declaration order with perfect forwarding. The static `FArdaMyNode::Invoke` and generic `Edit.Invoke<FArdaMyNode>` entries support the same positional form. Prefer a parameter struct or initializer for many optional fields and diagnostic names. Obtain typed output variables with the returned operation's `GetBuffer(Slot)` / `GetTexture(Slot)` before committing or canceling the edit; typed variables may then live as long as their graph resource. Every invocation creates a fresh operation with an automatic diagnostic name and uses the normal registry, validation, resource declaration and preparation path. A node may expose domain-specific static functions that build its parameters, call inherited `Invoke`, and return typed outputs. One free function name may use resource-type overloads to select different node classes, as `CopyTo` does for all four buffer/texture source and destination combinations. Keep overloaded facade function names distinct from global callable object names. Keep registration and dependency declarations in the node definition; do not introduce a separate facade registry.

Use `Edit.Invoke<Node>(Parameters)` for generic typed code and
`Edit.Invoke(DefinitionName, Parameters)` for registered discovery. Both static and
typed invocation accept an initializer callback filling `Node::FArdaParameters`
and an optional diagnostic name. The callback runs synchronously and may return
`FArdaRHIStatus`; it must not retain the temporary reference or edit the graph.
Every call creates a fresh operation, and duplicate explicit names fail. Raw graph
mutation and node attachment are private implementation details.

Derive class-authored operations from `TArdaDependencyNode<Derived, Parameters, Kind>` or its graphics, compute, copy, CUDA or synchronization specialization in `ArdaDependencyNode.h`. Implement static `GetMetadata`, `GetCanonicalKey`, `Describe` and the domain's execution hook: `Record` for native work or `PrepareCuda` for CUDA sequences. Synchronization nodes may omit execution. The base enforces this contract at compile time and owns invocation, registration, attachment, parameter retention and preparation lifetime. Do not duplicate or hide its invocation/attachment/registration methods in derived nodes.

Keep shader registration, binding metadata, layout creation and fixed pipeline configuration inside each node's implementation. Declare an opaque nested `FArdaState` and implement `Prepare(Device)` for immutable setup shared by live instances on that device. Declare `FArdaInstanceState` and implement `CreateInstance(Device, Parameters, State)` for private mutable bindings or parameter-dependent setup. Defaults handle nodes without setup. Validate inputs through `Validate`; return failures and never submit GPU work from attachment hooks. Device state is weakly cached by the base and strongly retained only by live bound instances. Shared state must be immutable; different nodes may record concurrently.

Encode semantic values and full resource identities in canonical keys without structure padding. Derive accesses, pipelines, cost and workspace from the same frozen parameters. Keep semantic pointees immutable; document deliberately dynamic inputs and their synchronization contract. Registry discovery uses `Edit.Invoke` with the registered public parameter schema; prepared execution state remains private.

Declare every resource access with its direction and precise byte or texture-subresource range. A region with one producing node keeps attachment-independent producer-to-reader ordering. When two or more distinct nodes write the same region, attach its operations in their intended execution order: overlapping writes and intervening reads follow original successful attachment order. A read observes the preceding write; an owned read before the first write fails, while an import may supply initial contents. Disjoint regions remain independent. Repeated writes reuse the same logical handle and physical graph resource; declare separate outputs when older values must remain available.

Operation call order determines repeated-write semantics; cancellation restores
the starting construction. Use `Edit.Order(Producer, Consumer)` for effects that
resources do not describe. These constraints cannot contradict resource hazards. Record through `FArdaDependencyExecutionContext`, resolve only declared resources and propagate RHI failures. Supply stages and settings for automatic pipeline inference instead of hiding manual native PSO creation inside ordinary graph operations.

## Edits and execution

Restrict mutations to explicit edit transactions. Use the scoped `FArdaGraphEdit` facade exclusively: construction begins the transaction, `Commit()` checks authoring failures and queues the closed edit, and `Cancel()` or destruction discards an uncommitted transaction. Preserve the first authoring failure in the edit and stop subsequent facade mutations; programs can check one commit result without accepting a partially authored graph. Resource handles must remain generation-checked across cancellation, removal, revision changes and slot reuse.

Describe each frame's desired operations in a fresh construction on the same graph. Multiple committed edits form an ordered queue; `Compile()` combines their semantic hashes and selects a cached persistent revision or compiles a new one. A successful compile clears the queue and locks editing until execution is attempted. `Execute()` or `Submit()` unlocks the next construction; the next ordinary edit starts empty. Repeated execution without new edits remains valid. Change options through `Edit.SetOptions`, retain outputs through `Edit.Keep`, and omit unwanted operations from the next construction.

Keep revision identity sensitive to edit order, semantic node parameters, resource descriptors and imported native identities. Preserve canonical equality checks and retained pointee lifetime; an address or hash alone must not hide a semantic change. Expose cache invalidation and forced compilation without forcing callers to remove nodes. Failed compilation retains queued work for another correction edit or `DiscardPendingEdits()`.

Treat completion tickets and retained input/output lifetime as part of the operation contract. Readback results are available only after successful `Execute()` / `Wait()` completion. Keep compiler and native execution details private.

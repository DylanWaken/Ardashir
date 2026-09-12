# Render graph documentation

Start with the [persistent dependency graph and ArdaInductor guide](ArdaInductor.md) for edits, registered typed nodes, scheduling, CUDA sequences and memory planning. The [pipeline guide](ArdaInductor-Pipelines.md) explains stage contributions, named slots, ambiguity, native PSO creation and reuse.

The [compilation and scheduling chapter](compilation.html) explains all implemented algorithms, their equations and tie-breaking rules, the allocation/cost model, adaptive search and worked scheduling examples.

The [overview](index.html), [getting started example](getting-started.html), [renderer guide](quick-guide.html) and [execution guide](execution.html) all use `FArdaDependencyGraph`. [GPU telemetry and background scheduling](ArdaInductor.md#gpu-telemetry-and-background-scheduling) covers optional per-node timing, EMA/history, nonblocking collection and schedule adoption after frame retirement. Both timing and automatic tuning are disabled by default. The [API reference](api-reference.html) inventories the current public graph headers. `ArdaRenderGraph.h` is an umbrella for the persistent APIs.

Regenerate and validate the inventory using the commands in [the backend documentation maintenance guide](../ArdaBackend/README.md#keeping-the-docs-synchronized).

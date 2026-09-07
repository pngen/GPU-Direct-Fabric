# GPU Direct Fabric

**GPU Direct Fabric** is a vendor-neutral C++20 control runtime for discovering,
validating, selecting, explaining, governing, and fencing GPU-direct data paths.

It does **not** implement an RDMA stack, a storage filesystem, a collective
library, a scheduler, or a bandwidth governor. Its single question is:

> Can this specific governed data path be used directly now, and under what
> authority?

## The systems question

GPU Direct Fabric answers whether data can move directly between accelerator
memory and a peer device, network endpoint, or storage path under the current
hardware, registration, topology, compatibility, ownership, and execution
authority — and if the preferred direct path cannot be used, which fallback is
valid, why, and under what current evidence.

The core thesis is:

> A GPU pointer is not a transferable resource merely because it exists.
>
> A direct path is valid only when the memory, endpoint, registration, topology,
> transport capability, ownership, generation, and execution authority required
> by that path remain current.

If any of those assumptions become stale, the runtime invalidates or revalidates
the path before use.

## Exact systems boundary

GPU Direct Fabric **owns** the identity and lifecycle of governed transfer
buffers as they relate to direct-path eligibility; direct-path capability
discovery; path requirement modeling; memory-domain classification;
peer/endpoint compatibility; registration evidence and generations; registration
and direct-path authority; direct-path and fallback candidate enumeration;
deterministic path selection and explanations; transfer planning; stale-path,
stale-registration, endpoint-generation, worker-incarnation, and coordinator-epoch
fencing; completion receipts; integrity verification; conservative recovery; and
the REAL / SYNTHETIC / UNSUPPORTED classification.

GPU Direct Fabric **does not own** general-purpose RDMA registration, verbs
provider implementation, NIC queue-pair lifecycle, NVLink/NVSwitch topology,
generic PCIe topology, arbitrary network routing, collective algorithms or
scheduling, MPI, NCCL, storage filesystem or object-store semantics, generic DMA
or buffer-allocation policy, Resource Broker arbitration, model/state placement,
bandwidth arbitration, congestion control, communication scheduling, DPU
programming, NIC residency, storage scheduling, or process-wide CUDA memory
management.

Neighboring runtimes it deliberately does **not** absorb: `RDMA Buffer`,
`NVLink Fabric`, `NVSwitch Fabric`, `PCIe Fabric`, `Storage Fabric`,
`Bandwidth Governor`, `Congestion Fabric`, `Communication Planner`,
`Fabric Scheduler`, `NIC Residency`, `DPU Fabric`, `Resource Broker`,
`Unified Buffer`, and `Transfer Fabric`. GPU Direct Fabric governs the
direct-vs-fallback decision and direct-path-specific authority; actual general
transfer execution belongs to the separate Transfer Fabric boundary, which this
runtime does not cross.

## Path classes

Data paths are modeled explicitly. Direct classes are only exposed as REAL when
the actual backend proves their support and the runtime holds sufficient evidence
to authorize them. Reference classes include:

`GPU_TO_GPU_DIRECT`, `GPU_TO_NIC_DIRECT`, `NIC_TO_GPU_DIRECT`,
`GPU_TO_STORAGE_DIRECT`, `STORAGE_TO_GPU_DIRECT`, `GPU_TO_HOST_PINNED`,
`HOST_PINNED_TO_GPU`, `GPU_TO_HOST_STAGED_TO_NIC`,
`NIC_TO_HOST_STAGED_TO_GPU`, `GPU_TO_HOST_STAGED_TO_STORAGE`,
`STORAGE_TO_HOST_STAGED_TO_GPU`.

Fallback classes are first-class. A fallback is never silently substituted
without being visible in the decision. A caller may require `DIRECT_ONLY`, or
allow `PREFER_DIRECT`, `ALLOW_STAGING`, or `BEST_AVAILABLE`. A fallback decision
explains why the direct path failed, which requirement was unsatisfied, which
fallback was selected, the additional copies/stages involved, expected semantic
differences, and the provenance/evidence freshness.

## Memory domains

Memory domains are modeled explicitly: `HOST_PAGEABLE`, `HOST_PINNED`,
`CUDA_DEVICE`, `CUDA_MANAGED`, `PEER_DEVICE`, `REGISTERED_GPU_MEMORY`,
`REGISTERED_HOST_MEMORY`, `STORAGE_VISIBLE_BUFFER`, `NIC_VISIBLE_BUFFER`,
`UNKNOWN`. The runtime never infers `CUDA_DEVICE -> GPUDIRECT_ELIGIBLE` or
`HOST_PINNED -> RDMA_REGISTERED`; eligibility depends on current backend
evidence.

## Registration semantics and generations

Registration is a first-class governed state. A buffer may exist without being
registered; a registration may exist without being current. Lifecycle includes
`UNREGISTERED`, `REGISTERING`, `REGISTERED`, `REVALIDATION_REQUIRED`,
`DEREGISTERING`, `RETIRED`, `FAILED`, `UNSUPPORTED`.

Everything identity-bearing is strongly typed and never lowered to a raw integer
or string: `BufferId`, `BufferGeneration`, `RegistrationId`,
`RegistrationGeneration`, `DeviceId`, `DeviceGeneration`, `EndpointId`,
`EndpointGeneration`, `NicId`, `NicGeneration`, `StorageEndpointId`,
`StorageEndpointGeneration`, `PathId`, `TransferPlanId`, `TransferAttemptId`,
`VerificationId`, `WorkerId`, `WorkerBootId`, `CoordinatorEpoch`, and
`CapabilityObservationId`. Generations never decrease. Raw CUDA pointers and
provider handles are transient process-local evidence, never durable identities,
and are never serialized as identities.

Duplicate idempotent registration does not double-count; conflicting registration
fails clearly; stale deregistration cannot destroy a newer registration.

## Authority, workers, and epochs

Evidence that originates from a worker is fenced by an explicit
`Authority { WorkerId, WorkerBootId, CoordinatorEpoch }`. A stale `WorkerBootId`
or `CoordinatorEpoch` cannot mutate current state. On real process death the
worker's process-owned dynamic registrations are marked
`REVALIDATION_REQUIRED`; resurrecting a dead worker requires a fresh boot id.
A coordinator restart persists durable logical state, advances the coordinator
epoch, clears live worker authority, and requires fresh evidence before a direct
plan can execute. Persistence never turns a dead RDMA registration into a live
one.

## Path eligibility

Eligibility outcomes include `DIRECT_ALLOWED`, `DIRECT_ALLOWED_DEGRADED`,
`DIRECT_UNSUPPORTED`, `REGISTRATION_REQUIRED`, `REGISTRATION_STALE`,
`ENDPOINT_STALE`, `DEVICE_STALE`, `TOPOLOGY_INCOMPATIBLE`,
`PROVIDER_UNAVAILABLE`, `FALLBACK_REQUIRED`, `NO_VALID_PATH`,
`REVALIDATION_REQUIRED`, `INSUFFICIENT_EVIDENCE`, `POLICY_REJECTED`,
`INVALID_BUFFER`, `UNSUPPORTED`. `UNKNOWN` never silently becomes
`DIRECT_ALLOWED`; the runtime fails closed. Each decision carries structured
requirement violations, the raw direct-path outcome, and the final
(fallback-aware) outcome, so inspection tooling can see exactly why a path is
eligible or ineligible.

## Transfer lifecycle

The runtime distinguishes a `plan`, an `attempt`, and a `completion`. Submission
is not completion. States include `PLANNED`, `AUTHORIZED`, `SUBMITTED`,
`IN_FLIGHT`, `COMPLETED`, `VERIFIED`, `FAILED`, `CANCELLED`, `STALE`,
`REVALIDATION_REQUIRED`. Plans are generation-bound; a plan built against
`RegistrationGeneration N` fails if the registration advances or is retired
before execution. A stale attempt cannot publish a current completion.
Duplicate completion is idempotent; conflicting completion is rejected.
Cancellation is real: a cancelled operation never later reports success, and a
cancelled transfer attempt cannot publish a verified completion after
cancellation wins the authority race. Where real data movement is exercised,
integrity is verified against a CPU-side expected representation; synthetic
integrity remains labeled synthetic.

## Persistence

Durable state, in a versioned format with a length prefix, CRC32, bounded sizes,
checked arithmetic, and atomic replacement, includes logical buffer identity
metadata, policy, stable device/endpoint identity, supported static capability
knowledge, prior observations for historical inspection, transfer history,
verification receipts, and durable configuration. Process-local raw pointers,
RDMA memory-region handles, verbs object addresses, process-local CUDA ordinals,
live provider handles, process-local registrations, active queue pairs,
temporary staging pointers, and live endpoint reachability are never persisted as
current authority. Corruption, truncation, trailing garbage, impossible enum
values, dangling references, invalid generations, absurd counts, overflow,
unsupported mandatory future versions, and malformed path plans are all rejected.

## Concurrency

The coordinator uses a single mutex with value-snapshot read surfaces; reads
return copies so no lock is held across caller code, and no read-lock is ever
upgraded to a write-lock on the same lock. A manual deadlock / lock-reentrancy
audit was performed over registration callbacks, endpoint failure during plan
queries, deregistration racing transfer begin, worker death racing completion
publication, cancellation racing completion, buffer generation advance racing
registration, persistence racing registration mutation, shutdown holding state
while joining, callbacks beneath locks, provider callbacks re-entering mutation,
and lock ordering. Deterministic race tests verify the semantics, not just
crash-freedom.

## Backend architecture

The core is vendor-neutral and has no external dependency. Optional backends are
separate libraries built conditionally and exported:

- `gdf_core` — the vendor-neutral control runtime.
- `gdf_synthetic` — a rigorous synthetic backend for semantics unavailable on
  the local hardware. Synthesized GPU→NIC, NIC→GPU, GPU→storage, storage→GPU,
  registration required/stale, buffer/NIC/storage generation advance, endpoint
  failure, provider unavailable, topology incompatibility, degraded path,
  policy rejection, fallback allowed/prohibited, direct-only, stale plan,
  stale completion, worker death, coordinator restart, conflicting registration,
  duplicate idempotent registration, deregistration race, synthetic corruption,
  fallback integrity, and unsupported backend.
- `gdf_cuda` — the real CUDA backend (device discovery, real device + pinned host
  allocation, H2D, kernel, D2H, CPU parity, baseline return). It only reports
  REAL observations about CUDA device, GPU memory, and pinned host memory; it
  never fabricates GPUDirect RDMA / GDS support.
- `gdf_unsupported` — honest representation of RDMA / GPUDirect Storage /
  generic providers that are not present on the platform (everything classed
  `UNSUPPORTED`).

## REAL / SYNTHETIC / UNSUPPORTED

Every hardware-facing observation and outcome carries an explicit support
classification (`REAL`, `SYNTHETIC`, `UNSUPPORTED`) plus a provenance
(`CUDA_RUNTIME`, `CUDA_DRIVER`, `NVML`, `OPERATING_SYSTEM`, `RDMA_PROVIDER`,
`VERBS`, `DMA_BUF`, `STORAGE_BACKEND`, `NVIDIA_DRIVER`, `BENCHMARK`, `PERSISTED`,
`DERIVED`, `SYNTHETIC_FIXTURE`). Persistence preserves provenance; synthetic
state never becomes REAL after reload.

## Build

Prerequisites: a C++20 compiler, CMake ≥ 3.22, and (optionally) the CUDA toolkit
for the `gdf_cuda` backend.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CUDA_ARCHITECTURES=native
cmake --build build --config Release
```

Build with `/W4 /WX` on MSVC (zero first-party warnings in Debug and Release).

## Tests

```sh
ctest --test-dir build -C Release --output-on-failure
```

The suite covers core invariants, path outcomes, fallback, transfer authority,
stale-plan generation, worker incarnation fencing, stale deregistration,
persistence corruption/truncation/trailing-garbage/absurd-length rejection,
malformed protocol framing, real CUDA validation, the real worker-death proof
(real OS process termination), the coordinator-restart recovery proof, a seeded
property/randomized test (emits seed, event index, and reproduction sequence on
failure), and deterministic race tests.

## Examples and CLI

Installed-API examples are in `examples/`. The inspection CLI is
`gpu_direct_fabric_inspect` with subcommands such as `list-devices`,
`providers`, `buffers`, `capabilities`, `registrations`, `cuda-self-test`,
`query-eligibility`, and `stale`, plus `--json` for stable machine-readable
output.

## Installation and downstream integration

```sh
cmake --install build --prefix /some/prefix
```

Install produces an exported CMake package. A downstream project can then use:

```cmake
find_package(GPUDirectFabric REQUIRED)
target_link_libraries(app PRIVATE GPUDirectFabric::gdf_core GPUDirectFabric::gdf_synthetic)
```

Downstream integration into an adopter's transport/storage stack is intentionally
outside this runtime's boundary.

## Hardware validation (local machine)

Local machine: NVIDIA GeForce RTX 5090, compute capability 12.0, CUDA 12.9
toolkit, MSVC 2022 17.14 (toolset 14.44), Windows 11 / SDK 26100.

- CUDA device discovery: **REAL PASS**
- real CUDA allocation: **REAL PASS**
- real pinned host allocation: **REAL PASS**
- real CUDA kernel: **REAL PASS**
- CPU parity: **REAL PASS**
- device-memory baseline return: **REAL PASS**
- multiple physical GPUs / CUDA peer access: **UNSUPPORTED** (single WDDM GPU)
- RDMA-capable NIC presence / RDMA provider availability: **UNSUPPORTED**
- real GPU-memory registration for RDMA / real GPUDirect RDMA transfer:
  **UNSUPPORTED**
- GPUDirect Storage runtime / real GDS registration / direct storage↔GPU: **UNSUPPORTED**
- synthetic GPU→NIC / NIC→GPU / GPU→storage / storage→GPU paths: **SYNTHETIC PASS**
- synthetic stale-registration fencing, direct→fallback recovery: **SYNTHETIC PASS**

## Genuine limitations

- No real GPUDirect RDMA or GPUDirect Storage path exists on the local consumer
  WDDM platform; those paths are classified `UNSUPPORTED` and never fabricated.
- Host staging is **not** GPUDirect. Synthetic direct paths remain synthetic.
- Raw CUDA pointers are not durable identities.
- The core runtime does not itself implement a general RDMA stack, and it does
  not replace NCCL/MPI.
- Integration into an adopter's transport/storage stack is deliberately
  downstream of this runtime's boundary.
- Sanitizer coverage is limited to the CUDA-free core subset (see the
  sanitizer section of the release report).

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.

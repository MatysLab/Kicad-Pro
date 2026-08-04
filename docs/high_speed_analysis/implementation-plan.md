# KiCad Implementation Plan

The estimates below are sequencing aids, not commitments. Each phase should be merged behind an
experimental feature flag until its validation gates pass.

## 1. Existing KiCad capabilities to reuse

The repository already contains important foundations:

- `pcbnew/board_stackup_manager`: stackup, dielectric, and copper definitions;
- `pcb_calculator/transline`: analytic microstrip, stripline, coplanar, coupled-line models;
- `eeschema/sim`: simulator UI, plotting, SPICE model abstraction, and ngspice integration;
- `eeschema/sim/kibis`: IBIS parsing/model construction;
- `qa/tests/spice` and `qa/tools/ibis`: IBIS/SPICE test assets;
- PCB connectivity, netclasses, differential pairs, zones, geometry, DRC, and background-job UI
  patterns elsewhere in `pcbnew` and `common`.

The new work should extract reusable libraries rather than copying calculator or simulator code
into PCB Editor.

## 2. Proposed module boundaries

```text
common/high_speed/
  analysis_snapshot.*       immutable board/model snapshot
  units_and_frequency.*     SI-only internal units and grids
  material_model.*          Dk/Df dispersion and conductor models
  nport_network.*           S/Y/Z/ABCD/mixed-mode operations
  touchstone_parser.*       Touchstone 1.x/2.x parser and writer
  rational_model.*          stable vector fitting and diagnostics
  analysis_result.*         provenance, warnings, convergence, datasets

pcbnew/high_speed/
  layout_extractor.*        route segmentation, topology, return paths
  cross_section_extractor.* local trace/dielectric geometry
  via_extractor.*           via/pad/antipad/stub/return geometry
  coupled_region_finder.*   parallel/coupled route intervals
  pdn_extractor.*           power/ground geometry and ports
  analysis_controller.*     jobs, cache, cancellation, UI events
  overlays/                 impedance, delay, coupling, PDN maps

simulation/high_speed/
  rlgc_solver.*             analytic and quasi-static interfaces
  channel_assembler.*       network connection and termination
  transient_engine.*        convolution/circuit transient adapters
  statistical_eye.*         ISI/jitter/noise probability engine
  ibis_adapter.*            shared access to existing KiCad IBIS
  ami_broker_protocol.*     out-of-process AMI RPC
  ddr_engine.*              groups, timing, leveling, margin
  pdn_engine.*              Z(f), resonance, decap optimizer
  field_solver_adapter.*    solver-neutral job/result contract
```

Exact top-level directories should follow KiCad maintainer guidance. The important constraint is
that physics/network code is UI-independent and testable.

## 3. Core data contracts

### Analysis snapshot

```text
AnalysisSnapshot
  board_hash
  coordinate_system and SI unit scale
  stackup/material versions
  conductors and dielectrics
  nets, routes, vias, zones, components, ports
  model references and content hashes
  constraints and analysis configuration
```

Creation occurs on the UI thread or under the board's normal synchronization. Solvers receive only
the immutable snapshot.

### N-port network

```text
NPortNetwork
  frequency_hz[]
  complex matrix data[f][port][port]
  parameter_kind (S/Y/Z/...)
  wave/reference convention
  per-port reference impedance
  port names, modes, locations, reference conductors
  passivity/causality/reciprocity diagnostics
  provenance
```

### Result bundle

```text
AnalysisResult
  result_type and schema_version
  input/config/model hashes
  solver name/version
  status, warnings, convergence
  axes and typed datasets
  board object -> result mapping
  summary limits and violations
```

Store configuration and small summaries in the project; store large caches/results in a
project-adjacent cache keyed by content hash. Never embed vendor AMI binaries or large field meshes
in the board file.

## 4. Work phases

### Phase 0 — specifications and foundations (4–8 engineer-weeks)

Deliverables:

- analysis snapshot and stable object identifiers;
- typed units/frequency grids and provenance schema;
- cancellable background-job framework with progress and cache;
- model/port assignment UI prototype;
- corpus licensing and security review.

Exit gates:

- editing the board during a job cannot corrupt or change the running result;
- identical snapshot/config/model hashes produce identical cached results;
- cancellation and crash recovery leave no project mutation.

### Phase 1 — network core and Touchstone (8–12 engineer-weeks)

Deliverables:

- Touchstone 1.x and 2.0/2.1 parser/writer;
- S/Y/Z/ABCD conversion, renormalization, reorder, termination, connection, cascade;
- single-ended/mixed-mode transforms;
- interpolation, DC/high-frequency diagnostics, passivity/causality checks;
- frequency plots, Smith chart reuse/integration, impulse/step preview.

Exit gates:

- official/contributed Touchstone corpus parses with expected diagnostics;
- round-trip tests preserve values within declared precision;
- N-port operations correlate with an independent implementation such as scikit-rf;
- malformed or non-passive data never silently proceeds to a “valid” transient result.

### Phase 2 — layout extraction and fast SI screening (10–16 engineer-weeks)

Deliverables:

- routed topology graph with branches/stubs;
- cross-section sampling along real tracks;
- fast frequency-dependent single-ended/differential RLGC;
- via Tier-1 compact model;
- propagation delay/loss/impedance overlays;
- coupled-region finder and crosstalk screening.

Exit gates:

- analytic canonical geometries match known formulas and the existing calculator;
- nonuniform routes segment deterministically;
- overlays update incrementally from board-change regions;
- warnings identify missing reference planes, plane void crossings, and invalid stackups.

### Phase 3 — end-to-end transient SI (10–16 engineer-weeks)

Deliverables:

- channel assembler for packages, PCB segments, vias, connectors, and terminations;
- lossless/lossy/coupled line export to the existing ngspice simulation layer;
- frequency-domain convolution engine for linear channels;
- source/load waveform and probe configuration;
- crosstalk with multiple timed aggressors.

Exit gates:

- canonical lines correlate with ngspice LTRA/CPL and analytical delay/loss;
- discontinuity/channel cascades correlate with independent N-port simulation;
- time-grid/bandwidth/DC warnings are actionable and reproducible.

Ngspice already provides LTRA and coupled-line models and is mostly modified-BSD licensed; see its
[developer/license information](https://ngspice.sourceforge.io/devel.html).

### Phase 4 — IBIS 8.0 conformance (12–20 engineer-weeks)

Deliverables:

- gap analysis of current `eeschema/sim/kibis` against IBIS 8.0;
- complete corners, differential models, package/interconnect references, power-aware data needed
  by the target analyses;
- model browser, pin/model mapping, validation report, and fixture plots;
- official `ibischk` correlation harness where licensing permits.

Exit gates:

- frozen public/vendor-approved corpus with expected parser messages;
- waveform/I-V fixture tests correlate against known reference simulations;
- unsupported keywords produce explicit capability warnings, never silent omission.

The official IBIS Open Forum [IBISCHK page](https://ibis.org/ibischk7/ibischk7.htm) documents the
golden parser and its licensing. A legal review is required before redistributing any executable or
source.

### Phase 5 — eye, BER, and IBIS-AMI (16–28 engineer-weeks)

Deliverables:

- time-domain eye with deterministic pattern/clock configuration;
- statistical LTI eye, bathtub, and BER contour;
- jitter/noise taxonomy aligned with IEEE 2414 and IBIS-AMI;
- PAM2/PAM4 initially, generalized PAM-n data model;
- out-of-process AMI broker for Init/GetWave/Close and current declared features;
- parameter editor, sweep/optimization, and model execution log.

Security gates:

- no AMI binary loads into the KiCad UI process;
- broker runs with a restricted working directory, no network, and resource limits;
- crashes/hangs produce a model error and broker restart, not project loss;
- binary hash, signer/source, platform, and user trust decision are recorded.

Accuracy gates:

- statistical/time-domain agreement for LTI reference models;
- published IBIS-AMI test models produce expected waveforms/parameters;
- BER results include method, sample/statistical assumptions, and confidence/validity limits.

### Phase 6 — DDR4/DDR5 analysis (12–20 engineer-weeks)

Deliverables:

- interactive controller/device and group assignment;
- CK/CA/DQS/DQ topology and fly-by-order validation;
- electrical flight time including package models and layer/via changes;
- per-byte/nibble DQ-to-DQS, CK-to-DQS, CA-to-CK tables;
- write/read leveling range, tap quantization, residual skew, setup/hold margin;
- topology-aware SI sweeps and voltage/timing derating;
- importable vendor/controller timing profile rather than hard-coded universal numbers.

Exit gates:

- timing equations and sign conventions verified on hand-worked fixtures;
- vendor public reference layouts reproduce published constraint outcomes;
- each margin traces back to geometry, package delay, protocol term, and model source;
- unavailable JEDEC/vendor data blocks signoff and clearly labels exploratory mode.

### Phase 7 — PDN and decoupling optimization (16–28 engineer-weeks)

Deliverables:

- rail/ground recognition and VRM/load/capacitor ports;
- capacitor RLC/S-parameter library with bias/tolerance/temperature corners;
- plane/trace lumped and distributed extraction tiers;
- multiport `Zii/Zij(f)`, resonance/Q attribution, and spatial maps;
- target-impedance and transient current-profile checks;
- decap what-if and constrained optimization by part/count/site/cost/area.

Exit gates:

- canonical RLC networks match analytical resonance and anti-resonance;
- multiport results correlate with SPICE and field-solver references;
- optimizer is deterministic for a fixed seed/config and reports sensitivity/robustness;
- recommendations never use sites that violate placement, assembly, or connectivity constraints.

### Phase 8 — field-solver adapters (12–24 engineer-weeks per mature adapter)

Deliverables:

- solver-neutral geometry/port/material job description;
- local 2D quasi-static extractor adapter first;
- optional openEMS 3D EC-FDTD adapter for bounded structures;
- mesh preview, convergence study, de-embedding, result import, and cache;
- adapter capability/version discovery.

Start with external-process integration. [openEMS](https://docs.openems.de/intro.html) is GPLv3
and supports graded meshes, dispersive materials, PML, and Python/Octave interfaces. Keeping a
process/file boundary also protects KiCad from solver crashes and preserves adapter neutrality.

## 5. UI plan

Add an **Analyze** workspace in PCB Editor rather than crowding Board Setup:

- **Models and ports:** assign Tx/Rx/package/connector/PDN models and trust status;
- **Analysis setup:** type, nets/groups, frequency/data rate, corners, fidelity tier;
- **Run monitor:** immutable snapshot ID, progress, cancel, cache hit/miss, solver log;
- **Results:** plots/tables plus synchronized board overlays;
- **Explain result:** selected violation shows the geometry/model/equation terms that created it;
- **Compare:** nominal vs corner, before vs after, or two layout revisions.

Board Setup remains the source for stackup/materials and should gain frequency-dependent material
model references and validation state, not full analysis controls.

## 6. Performance plan

- Hash and cache at three levels: extracted geometry, electrical block, final analysis.
- Invalidate only blocks intersecting changed board objects or changed model/config hashes.
- Run extraction and solving off the UI thread from immutable data.
- Stream decimated plot data first, then full-resolution results.
- Use sparse matrices/eigensolvers for coupled/PDN systems.
- Batch parameter sweeps and reuse factorizations/network blocks.
- Set hard budgets for mesh cells, ports, frequency points, time samples, and AMI runtime.
- Never recompute field-solver blocks during ordinary pan/zoom/selection operations.

## 7. Legal and supply-chain work

- Review licenses for parser corpora, standards excerpts, solver libraries, and bundled examples.
- Record provenance and redistribution rights for every model shipped in tests/demos.
- Treat vendor IBIS/AMI/S-parameter models as user-provided unless redistribution is explicit.
- Verify whether official IBIS/Touchstone checker executables may be downloaded on demand or only
  used in developer CI.
- Add signature/hash allow-list support for AMI executables and disclose native-code risk.

## 8. Definition of done for the overall program

- A result can be reproduced from a clean checkout plus the declared model files.
- Every result identifies model tier, validity range, assumptions, warnings, and source hashes.
- Layout overlays link back to the exact geometry and network block.
- Imported networks are checked for reference, bandwidth, passivity, and causality.
- IBIS/AMI failures cannot crash PCB Editor or modify the project.
- SI, DDR, and PI reference suites pass on all supported platforms.
- Documentation distinguishes screening, correlation, and signoff-grade workflows.


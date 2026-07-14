# Validation and Benchmark Plan

The project should not claim accuracy from visual plausibility. Each solver tier needs analytical,
cross-tool, and—where possible—measurement correlation.

## 1. Test pyramid

### Unit tests

- complex matrix operations and S/Y/Z/ABCD conversions;
- mixed-mode transforms and arbitrary port reorder;
- Touchstone lexical, semantic, and round-trip cases;
- material interpolation/dispersion and unit conversions;
- telegrapher equations, RLGC modes, line two-ports;
- impulse/pulse response and convolution;
- jitter/noise PDFs, CDFs, BER integration;
- DDR timing sign conventions and tap quantization;
- capacitor RLC, target impedance, resonance/anti-resonance.

### Golden component tests

- lossless 50-ohm line with known delay;
- lossy line with known `R'L'G'C'`;
- symmetric differential pair with known odd/even modes;
- coupled victim/aggressor line with known NEXT/FEXT response;
- open/short via stub quarter-wave resonance;
- passive reciprocal two/four-port and deliberately invalid networks;
- IBIS pullup/pulldown/clamp and waveform fixtures;
- two-capacitor anti-resonance and multiport PDN fixture.

### System tests

- PCB layout -> extraction -> overlay with deterministic object mapping;
- package + PCB + Touchstone connector + receiver end-to-end channel;
- time-domain eye vs statistical eye for an LTI channel;
- DDR byte lane with fly-by CK/CA and point-to-point DQ/DQS;
- VRM + planes + multiple capacitor sites + package/load PDN.

## 2. Independent correlation tools

Use at least two independent paths where feasible:

- ngspice LTRA/CPL for distributed-line transient correlation;
- scikit-rf for N-port import, conversion, cascade, and vector fitting;
- openEMS for selected 3D via/launch/coupling structures;
- hand calculations for canonical RLGC/RLC/timing cases;
- official IBIS/Touchstone checker tools subject to license;
- measured VNA/TDR fixtures when hardware becomes available.

[IEEE 370-2020](https://standards.ieee.org/ieee/370/6165/) and its public fixture data are the
recommended framework for interconnect measurement/de-embedding correlation.

## 3. Proposed numerical tolerances

These are initial engineering gates and must be refined per model tier:

| Test | Initial gate |
|---|---|
| Exact network algebra identity/round trip | relative error <= `1e-10` away from singularities |
| Touchstone text round trip | within declared source/output precision |
| Analytic uniform-line Z0/delay | <= 0.1% |
| 2D extracted canonical cross-section | <= 1% vs trusted reference mesh after convergence |
| Time-domain line delay | <= one internal time step and <= 1% |
| Passive-network eigenvalue test | no negative margin beyond numerical tolerance |
| Rational-fit response | configured max/RMS error plus stable poles and passivity |
| DDR hand-worked margins | <= 1 ps or declared rounding quantum |
| RLC resonance/anti-resonance | <= 0.5% frequency, <= 1% impedance magnitude |

Do not use one universal tolerance for 3D field results. Require a mesh-convergence curve and
comparison band relevant to the intended data rate/frequency.

## 4. Parser/model corpus

Create redistributable fixtures for:

- Touchstone 1.0/1.1/2.0/2.1, RI/MA/DB, Hz through GHz, full/lower/upper matrices,
  single-ended/mixed-mode, unequal references, noise, comments, malformed inputs;
- IBIS versions represented by KiCad's support target, including corners, differential models,
  package models, selectors, pin mapping, power-aware data, AMI references, and expected failures;
- AMI parameter grammar and broker API fixtures for supported platforms;
- material tables and causal dispersive fits;
- vendor-neutral capacitor/package S-parameters with explicit redistribution permission.

The [IBIS Open Forum tools page](https://ibis.org/tools/tools.htm) lists contributed model/test
tools. Corpus licenses must be recorded file by file.

## 5. Performance benchmarks

Track wall time, peak memory, cache reuse, and cancellation latency for:

- 2, 4, 8, 16, and 32 copper-layer boards;
- 100, 1,000, and 10,000 routed segments;
- 2, 8, 32, and 128 coupled conductors/ports where supported;
- 201, 1,001, and 10,001 frequency points;
- 1e5–1e8 equivalent bits for statistical/time-domain eye methods;
- 10, 100, and 1,000 candidate capacitor sites/parts;
- coarse-to-converged field meshes.

Interactive goals:

- snapshot progress appears within 100 ms;
- cancellation acknowledgment within 250 ms for in-process jobs;
- cached overlay reopen within one second for a medium board;
- UI remains responsive during extraction, fitting, simulation, and AMI execution.

## 6. Reliability and security tests

- truncated/corrupt/oversized model files;
- NaN/Inf, singular networks, nonmonotonic frequencies, and adversarial dimensions;
- AMI crash, hang, memory exhaustion, illegal file access, and incompatible architecture;
- solver crash and partial result files;
- board edit/close while jobs are running;
- stale cache and model hash mismatch;
- locale-independent decimal parsing and cross-platform deterministic serialization.

## 7. Release evidence

Each released analysis tier needs:

- a capability/limitation matrix;
- public golden fixtures and expected results;
- correlation plots and numeric error tables;
- performance baselines on supported platforms;
- model/parser compatibility matrix;
- security threat model for executable/external solvers;
- user documentation showing how to distinguish estimate, correlation, and signoff workflows.


# High-Speed Analysis Research and Implementation Plan

Status: research and architecture proposal; no solver implementation is included here.

This package defines a path for adding layout-aware signal-integrity (SI), power-integrity
(PI), serial-link, and DDR analysis to KiCad.  The central design principle is to calculate from
the actual PCB geometry and project models, while making the fidelity and assumptions of every
result explicit.

## Documents

- [Physics and algorithms](physics-and-algorithms.md) derives the main calculations and states
  where each model is valid.
- [Calculation flow](calculation-flow.md) shows how board data becomes SI/PI/DDR results.
- [Implementation plan](implementation-plan.md) maps the research onto KiCad modules and staged
  deliverables.
- [Validation plan](validation-plan.md) defines reference cases, correlation tests, and release
  gates.

## Requested capability map

| Capability | Required foundation | Primary result |
|---|---|---|
| High-speed signal simulation | layout extraction, driver/receiver models, channel solver | time-domain waveforms and margins |
| Layout-aware crosstalk | multiconductor RLGC and aggressor timing | NEXT/FEXT noise and victim eye degradation |
| SI and PI analysis | shared geometry/material database and frequency engine | channel quality and PDN impedance |
| Eye and BER analysis | pulse/impulse response, jitter/noise models, receiver sampler | eye contours, bathtub curves, BER estimate |
| IBIS and IBIS-AMI | standards-compliant parsers, model validation, executable sandbox | behavioral I/O and equalized SerDes simulation |
| Geometry-based line/via models | stackup, trace cross-sections, via/antipad/return geometry | RLGC, delay, loss, discontinuity models |
| Field-solver integration | solver-neutral geometry exchange and job manager | 2D/2.5D/3D extracted networks |
| End-to-end channels | N-port network algebra and Touchstone | cascaded package/board/connector/channel response |
| DDR4/DDR5 analysis | topology recognition, package delay, DQ/DQS/CK grouping | skew, setup/hold, leveling range, timing margin |
| Advanced PDN analysis | plane/rail extraction and component parasitics | impedance vs frequency and decap optimization |

## Architectural decisions

1. **One canonical analysis snapshot.** A frozen snapshot contains nets, topology, copper
   geometry, stackup, material models, components, ports, and simulation settings. Background
   jobs never read a board that is being edited.
2. **Tiered fidelity.** Every analysis selects one of four explicit tiers: rule/analytic,
   2D quasi-static, circuit/network, or 3D full-wave. A fast estimate must never be presented as
   field-solver signoff.
3. **A shared network representation.** Transmission lines, vias, packages, connectors,
   Touchstone files, and extracted structures become composable N-port blocks.
4. **SI and PI share geometry but not assumptions.** SI is normally a propagation/coupling
   problem; PI is a multiport impedance problem with voltage-dependent current demand. Their
   solver pipelines may share meshing, ports, and network math without pretending they are the
   same analysis.
5. **Reproducible and inspectable.** Results store source model hashes, board revision hash,
   solver/version, units, frequency/time grid, assumptions, warnings, and convergence data.
6. **External executable models are untrusted.** IBIS-AMI binaries run out of process with
   restricted filesystem/network access, time and memory limits, crash isolation, and an explicit
   user trust decision.

## Recommended delivery order

The useful first product is not a monolithic “simulate everything” button. The dependency-safe
sequence is:

1. Canonical board snapshot, ports, topology extraction, and analysis result format.
2. Touchstone 2.1 parsing plus N-port conversion, interpolation, cascade, passivity, and
   causality diagnostics.
3. Fast single-ended/differential RLGC, via discontinuity estimates, delay/loss maps, and
   geometry-aware crosstalk screening.
4. Transient channel simulation using existing KiCad/ngspice infrastructure where appropriate.
5. IBIS 8.0 behavioral support with official-parser correlation tests.
6. Statistical/time-domain eye and BER, followed by sandboxed IBIS-AMI.
7. DDR4/DDR5 topology/timing analysis.
8. PDN multiport extraction and decoupling optimization.
9. Optional 2D and 3D solver adapters for correlation and signoff-quality extraction.

## Standards baseline (July 2026)

- The [IBIS Open Forum specifications index](https://ibis.org/specs/specs.htm) lists IBIS 8.0
  as ratified in December 2025 and Touchstone 2.1 as the current ratified Touchstone version.
- [Touchstone 2.1](https://ibis.org/touchstone_ver2.1/touchstone_ver2_1.pdf) defines N-port
  network data, mixed-mode ordering, reference impedances, and noise information.
- [IEEE 370-2020](https://standards.ieee.org/ieee/370/6165/) provides practices and validation
  material for PCB interconnect characterization through 50 GHz.
- [IEEE 2414-2020](https://standards.ieee.org/ieee/2414/5935/) supplies consistent jitter and
  BER terminology.
- JEDEC DDR4/DDR5 requirements remain the normative protocol/electrical source. Public access
  and redistribution terms must be checked before including tables or derived rule sets.

## Scope and non-goals

- The proposed tools aid design exploration and verification; fabrication and silicon-vendor
  signoff data remain authoritative.
- A trace-width calculator is not a substitute for a frequency-dependent material model,
  conductor roughness, fabrication tolerance, or field-solver extraction.
- A geometrically clean eye generated from guessed transmitter/receiver models is not a valid
  BER prediction.
- Proprietary solver or model formats are not required for the core architecture; adapters may
  be added when licensing permits.


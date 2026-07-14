# Native High-Speed Analysis Implementation

This branch introduces the shared foundation for native layout-aware SI, PI, DDR, and field-solver
work. The implementation is deliberately staged: a result must never be presented as trustworthy
until its parser, network operations, extraction fidelity, and numerical assumptions have explicit
validation.

## Implemented in the first slice

- Canonical frequency-ordered complex N-port storage with per-port reference impedances.
- Touchstone 1.x, 2.0, and 2.1 conventional single-ended network import.
- RI, MA, and DB data conversion.
- Legacy and natural two-port matrix ordering.
- Full, lower-triangular, and upper-triangular matrix expansion.
- Explicit diagnostics for malformed, incomplete, mixed-mode, and unsupported data.
- A cancellable, solver-neutral external field-solver job/result interface.
- Field-solver job validation for geometry, frequency range, ports, references, and cache identity.
- Focused native QA coverage for matrix ordering, references, triangular expansion, malformed data,
  and field-solver jobs.

Touchstone values are retained in the parameter form declared by the source file. Parameter
conversion, renormalization, passivity enforcement, vector fitting, and mixed-mode transformation
belong to the next network-math slice rather than being approximated in the reader.

## Next native slices

1. N-port S/Y/Z conversion, renormalization, interpolation, connection, termination, mixed-mode
   transformation, passivity, and causality diagnostics.
2. Immutable PCB analysis snapshot and incremental extraction of tracks, arcs, vias, reference
   planes, layer transitions, and coupled regions.
3. Fast RLGC, impedance, delay, loss, return-path, and crosstalk overlays in PCB Editor.
4. Channel assembly using trace/via models, Touchstone packages/connectors, IBIS, and ngspice or
   frequency-domain convolution.
5. Eye/BER and sandboxed IBIS-AMI execution.
6. DDR4/DDR5 topology, flight-time, leveling, skew, and timing-margin analysis.
7. PDN multiport impedance, resonance attribution, and constrained decoupling optimization.
8. openEMS adapter: bounded-region geometry export, material and port mapping, mesh preview,
   external execution, de-embedding, S-parameter import, convergence evidence, and caching.

## openEMS integration boundary

openEMS remains an optional external process. KiCad owns the board snapshot, port definitions,
job identity, progress/cancellation, imported N-port result, and all downstream SI/PI analysis.
The adapter owns solver-specific geometry, meshing, process execution, and raw field data. This
keeps PCB Editor isolated from solver failures and allows other 2D or 3D solvers to implement the
same interface.

An openEMS run must not be considered valid unless its result records the board/geometry hash,
material model, port reference planes, mesh settings, boundary conditions, solver version,
de-embedding, frequency grid, termination criterion, and mesh-convergence comparison.

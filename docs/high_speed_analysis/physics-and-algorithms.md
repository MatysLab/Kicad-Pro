# Physics and Algorithms

This document is an implementation-oriented derivation, not a replacement for the cited
standards. Symbols use SI units internally. UI conversion happens only at input/output boundaries.

## 1. Common physical representation

Every analysis starts from a board snapshot containing:

- ordered copper/dielectric stackup;
- frequency-dependent relative permittivity `epsilon_r(f)` and loss tangent `tan_delta(f)`;
- copper conductivity, thickness, plating, and optional roughness parameters;
- routed centerlines and polygonal copper shapes;
- vias including drill, finished barrel, pad, antipad, span, stub, and nearby return vias;
- plane/zone geometry and voids;
- component pin/package mapping, model references, and analysis ports;
- net topology, differential-pair association, bus/byte-lane membership, and timing constraints.

The extractor divides a route wherever its cross-section or topology changes. Each uniform section
becomes a transmission-line block; bends, neck-downs, pads, vias, plane transitions, connectors,
and branches become discontinuity blocks. This segmentation is essential: a single “net length” is
not an electrical channel model.

### Material dispersion

PCB laminate `Dk` and `Df` depend on frequency and test method. Internally, materials need a
complex permittivity:

```text
epsilon*(omega) = epsilon'(omega) - j epsilon''(omega)
tan_delta(omega) = epsilon''(omega) / epsilon'(omega)
```

A constant `Dk/Df` model is acceptable only as an explicitly labeled narrow-band approximation.
Wideband transient work needs a causal dispersive fit (for example, Debye poles) so the
frequency-domain response produces a physical impulse response.

## 2. Transmission-line equations

For a uniform single conductor referenced to a return conductor, per-unit-length parameters are
`R'`, `L'`, `G'`, and `C'`. The frequency-domain telegrapher equations are:

```text
dV/dz = -(R' + j omega L') I
dI/dz = -(G' + j omega C') V
```

Therefore:

```text
gamma = alpha + j beta = sqrt((R' + j omega L') (G' + j omega C'))
Z0 = sqrt((R' + j omega L') / (G' + j omega C'))
vp = omega / beta
td = length / vp
```

The exact two-port of a uniform line of length `l` is:

```text
A = D = cosh(gamma l)
B = Z0 sinh(gamma l)
C = sinh(gamma l) / Z0
```

This ABCD block can be converted to S/Y/Z parameters or cascaded directly. In the low-loss limit,
`Z0 ~= sqrt(L'/C')` and `vp ~= 1/sqrt(L'C')`.

### Frequency-dependent loss

- Conductor loss increases as current crowds toward the surface. The classical skin depth is
  `delta = sqrt(2 rho / (omega mu))`; resistance must transition smoothly from DC to skin-effect
  behavior and account for plating/roughness when data is available.
- Dielectric loss follows the imaginary permittivity. A common low-loss approximation is
  `alpha_d ~= beta tan_delta / 2`, but the implementation should prefer the complex material model.
- Radiation and leakage are not represented by a pure 2D quasi-TEM line model; warn when geometry
  leaves the model's validity domain.

KiCad already contains analytic transmission-line calculators under `pcb_calculator/transline`.
They are a useful seed for fast estimates, but the analysis engine needs a unified, tested RLGC API
with frequency sweeps, uncertainty, and model-validity metadata.

## 3. Differential and multiconductor lines

For `N` coupled conductors, voltage/current and RLGC become vectors and matrices:

```text
dV/dz = -(R + j omega L) I
dI/dz = -(G + j omega C) V
```

Diagonal terms describe self behavior; off-diagonal terms describe capacitive and inductive
coupling. Solve the matrix eigenproblem to obtain propagation modes, modal impedances, and mode
conversion. For a symmetric differential pair:

```text
Zdiff = 2 Zodd
Zcommon = ZEven / 2
```

where `Zodd` and `Zeven` come from odd/even modal solutions. Mixed-mode S-parameters must preserve
the declared port ordering and reference impedance.

### Layout-aware crosstalk

The extractor must find parallel/coupled route intervals, layer relationships, reference planes,
and return-path discontinuities. For each coupled interval:

1. extract multiconductor RLGC;
2. solve the coupled network with source/load terminations;
3. align aggressor switching times and bit patterns;
4. superimpose only when the model is linear, otherwise use transient simulation;
5. report victim peak noise, integrated noise, timing shift, and eye degradation.

Simple spacing rules are useful screening metrics, not final crosstalk calculations. Near-end and
far-end crosstalk depend on both mutual capacitance and mutual inductance, line length, edge rate,
terminations, and propagation mismatch. A victim can pass a spacing rule and still fail because of
a long coupled run or a broken return path.

Ngspice supports lossy and coupled multiconductor line models; its documentation describes LTRA
and recursive-convolution CPL/TXL models. See the
[ngspice transmission-line manual](https://nmg.gitlab.io/ngspice-manual/transmissionlines.html).

## 4. Via and discontinuity modeling

A via is not only a series inductance. A practical hierarchy is:

### Tier 1: screening model

- series barrel resistance and inductance;
- shunt pad/antipad capacitance to reference planes;
- unused stub as a transmission-line resonator;
- return-path inductance from nearby stitching vias or plane-transition capacitors.

The first stub resonance is approximately quarter-wave:

```text
f_stub ~= vp / (4 l_stub)
```

This estimate is for triage only because pad stacks, antipads, plane cavities, and coupling shift
the resonance.

### Tier 2: geometry-derived compact model

Use a 2.5D/3D extractor to create a passive broadband N-port for the via plus local reference
structure. Ports must represent both signal and return currents. Differential vias require at least
a four-port mixed-mode representation.

### Tier 3: full-wave model

Export a bounded local volume including pads, antipads, barrels, planes, voids, and return vias to
a 3D solver. De-embed ports to stable reference planes and store the resulting S-parameter block.

## 5. N-port networks and Touchstone

For incident/reflected power waves `a` and `b`:

```text
b = S a
```

The analysis core needs robust conversion among S, Y, Z, ABCD/T, and mixed-mode forms. Operations
include renormalization, port reorder, termination, connection, cascade, de-embedding,
interpolation, and time-domain transform.

Required validation:

- frequency monotonicity and duplicate-point policy;
- consistent port count/order/reference impedance;
- reciprocity diagnostics where physically expected;
- passivity (`I - S^H S` positive semidefinite for power-wave conventions);
- causality and adequate low/high-frequency bandwidth;
- stable interpolation and extrapolation warnings;
- DC handling before inverse FFT;
- rational fitting with stable poles and passivity enforcement when a time-domain macromodel is
  required.

Touchstone 2.1 is the normative open file format for this work. It supports N-port data,
single-ended or mixed-mode ordering, and per-port references. See the
[official Touchstone 2.1 specification](https://ibis.org/touchstone_ver2.1/touchstone_ver2_1.pdf).
Vector fitting can represent sampled network responses as:

```text
H(s) = d + s e + sum_k c_k / (s - p_k)
```

with stable poles `p_k`; the fit still needs passivity and error checks. The
[scikit-rf vector-fitting documentation](https://scikit-rf-official.readthedocs.io/en/latest/tutorials/VectorFitting.html)
is a useful independent correlation implementation.

## 6. End-to-end channel simulation

The assembled channel is:

```text
driver -> Tx package -> PCB/vias -> connector/cable -> PCB/vias -> Rx package -> receiver
```

Each block has explicit ports and reference impedance. The engine must not cascade raw S-matrices
by element-wise multiplication; it must connect networks using valid multiport algebra or convert
compatible two-ports to chain parameters.

### Frequency-to-time path

1. assemble a causal/passive channel transfer function on a suitable frequency grid;
2. include source and load terminations;
3. obtain impulse response `h(t)` by a controlled inverse transform;
4. derive the unit-pulse response by integrating/differencing as appropriate;
5. convolve with a symbol sequence and transmitter waveform;
6. apply receiver filtering/equalization and sampling.

Frequency spacing determines the time record; maximum frequency determines time resolution.
Windowing, DC extrapolation, and truncation can change the eye, so those choices belong in the
result metadata.

## 7. IBIS and IBIS-AMI

### Traditional IBIS

An IBIS buffer is reconstructed from behavioral data rather than transistor netlists:

- pullup/pulldown and clamp I-V tables;
- rising/falling V-t waveforms under specified fixtures;
- package R/L/C or package/interconnect models;
- `C_comp`, pin mapping, corners, model selector, differential data, and power-aware extensions.

The simulator solves the nonlinear current contribution against the external network while using
waveform data to reproduce switching behavior. Table range, monotonicity, fixture consistency,
corner completeness, and clamp double-counting require validation.

KiCad already has IBIS parsing/model code under `eeschema/sim/kibis`, existing tests under
`qa/tests/spice`, and an ngspice-backed simulator. The first task is a conformance gap analysis,
not a second unrelated parser.

The [IBIS specifications index](https://ibis.org/specs/specs.htm) lists IBIS 8.0 as the current
ratified version. The official `ibischk` executable should be used as an independent acceptance
oracle; licensing must be reviewed before bundling it or its source.

### IBIS-AMI

An AMI model includes a parameter file and a platform executable exposing standardized entry
points. Depending on the model, the host uses:

- `AMI_Init` for an LTI/statistical response;
- `AMI_GetWave` for time-domain/non-LTI processing and clock information;
- `AMI_Close` for cleanup;
- current-version resolve/backchannel/test-data features when declared.

The [IBIS 8.0 specification](https://ibis.org/ver8.0/ver8_0.pdf) documents the current
dynamic-library interface and AMI reference flows. Because this runs third-party native code, the
host process must never load it directly into PCB Editor. Use a separate broker process with
architecture/version checks, file allow-listing, no network, memory/CPU/time limits, crash recovery,
deterministic logs, and explicit user consent.

## 8. Eye diagrams and BER

### Time-domain eye

Generate a sufficiently long waveform, recover or provide a sampling clock, and fold samples by
the unit interval `UI`. Track pattern, warm-up, equalizer state, jitter source, and noise source.

### Statistical eye

For an LTI channel, use the pulse response to enumerate/superpose intersymbol-interference states,
then convolve voltage and timing probability distributions. This reaches very low BER faster than
brute-force bits but is invalid for unmodeled nonlinear/time-varying behavior.

For a sample value with Gaussian noise:

```text
BER = 0.5 erfc(Q / sqrt(2))
Q = (mu_1 - mu_0) / (sigma_1 + sigma_0)
```

This closed form is only a Gaussian binary approximation. The production engine needs PAM-n
thresholds, deterministic/random/bounded jitter, data-dependent jitter, correlated noise,
equalizer adaptation, clock recovery, and confidence bounds. Report bathtub curves and BER
contours rather than only a visually open eye.

[IEEE 2414-2020](https://standards.ieee.org/ieee/2414/5935/) supplies consistent jitter/BER
definitions. IBIS-AMI defines statistical and time-domain reference flows; the IBIS Open Forum's
[reference-flow material](https://ibis.org/~ibisorg/summits/may23/leslie.pdf) illustrates pulse
response, jitter PDFs, eye, and BER flow.

## 9. DDR4/DDR5 analysis

DDR analysis is topology- and protocol-aware SI, not just length matching.

### Topology recognition

Identify controllers, devices/DIMMs, byte/nibble groups, CK, command/address/control, DQS pairs,
DQ/DM, terminations, fly-by order, branches, and package delays. The user must be able to correct
auto-detection.

### Electrical delay and skew

For a nonuniform route:

```text
t_flight = integral beta(omega, z) / omega dz
t_arrival = t_package_tx + t_flight + t_package_rx
skew(i, ref) = t_arrival(i) - t_arrival(ref)
```

Length-only matching is insufficient when routes use different layers, weave/material regions,
via counts, packages, or loading. Delay should be measured at a defined frequency or from a
time-domain threshold crossing, and the method must be reported.

### Timing budget

For each receiver and operating point:

```text
setup_margin = available_setup - flight_skew - Tx_uncertainty - Rx_setup - SI_derating
hold_margin  = available_hold  - flight_skew - Tx_uncertainty - Rx_hold  - SI_derating
```

The detailed signs and terms differ for read/write and protocol generation. Include package delay,
controller/DRAM timing, duty-cycle distortion, jitter, crosstalk, voltage threshold movement,
simultaneous switching, and training range.

### Leveling/training feasibility

- Write leveling checks whether programmable DQS delay can align write strobes to CK at each
  destination in fly-by order.
- Read leveling/gating checks whether each DQS/DQ group can be centered inside the controller's
  programmable capture window.
- Per-bit deskew checks remaining DQ-to-DQS spread against available taps and eye width.
- Command/address analysis remains referenced to CK and cannot be declared safe merely because
  data training exists.

The engine should calculate pre-training physical skew, required tap/range, residual skew after
quantization, and final margin. Vendor/controller limits are model inputs, not hard-coded universal
constants.

As a public implementation reference, AMD's current
[DDR5 timing rules](https://docs.amd.com/r/en-US/ug863-versal-pcb-design/Timing-Constraint-Rules-for-DDR5-Signals)
explicitly require package delays in skew calculations, and its
[general memory routing guidance](https://docs.amd.com/r/en-US/ug583-ultrascale-pcb-design/General-Memory-Routing-Guidelines)
explains the relation between fly-by skew and write leveling. Normative electrical/protocol values
must come from licensed/current JEDEC and silicon-vendor documentation selected by the user.

## 10. Power-integrity and PDN analysis

### Target impedance

The simplest constant target is:

```text
Z_target = DeltaV_allowed / DeltaI_step
```

Real loads have a current spectrum and time-domain limits, so support frequency-dependent targets
and transient checks. Passing a constant target is not automatically sufficient.

### Component and mounting model

A capacitor branch is approximately:

```text
Z_cap(omega) = ESR + j omega (ESL + L_mount) + 1/(j omega C)
f_SRF = 1 / (2 pi sqrt((ESL + L_mount) C))
```

Use vendor S-parameters or measured models when available. Placement changes mounting/plane-spread
inductance, so identical capacitors at different sites are not electrically identical.

### Multiport PDN

Extract `Z(f)` between VRM, capacitor, package, and observation ports:

```text
V(f) = Z(f) I(f)
```

`Z_ii` is driving-point impedance and `Z_ij` is transfer impedance. Plane pairs require a
cavity/distributed model above the range where lumped approximations are valid.

### Resonance and anti-resonance

Parallel branches can create a high-impedance peak where one branch is inductive and another is
capacitive. Detect local maxima, Q/bandwidth, mode/port participation, and expected voltage response
to the load spectrum. Do not optimize only capacitor count; optimize part/value/package,
placement, mounting loop, cost, area, and robustness over tolerance/bias/temperature.

The classic target-impedance methodology is described in research on
[power-distribution system design and capacitor selection](https://oamonitor.ireland.openaire.eu/rpo/rcsi/search/publication?pid=10.1109%2F6040.784476).
Experimental/analytical work on
[decoupling-capacitor impedance and anti-resonance](https://hajim.rochester.edu/ece/sites/friedman/papers/ICECS_04_Decap.pdf)
shows why mixed capacitor values can produce peaks. A modern open paper on
[PDN decap optimization](https://mst.elsevierpure.com/en/publications/decoupling-capacitor-optimization-to-achieve-target-impedance-in--2/)
includes stackup and placement effects.

## 11. Field-solver integration

### 2D quasi-static extraction

Solve electrostatic/magnetostatic cross-sections to obtain capacitance and inductance matrices,
then derive modal impedance/delay. This is fast for long uniform structures and should be the first
field-solver tier.

### 2.5D planar extraction

Use planar Green-function/MoM or equivalent methods for coupled traces, plane shapes, and many PCB
structures. It is efficient when vertical geometry is layered.

### 3D full-wave extraction

FEM/FDTD/MoM handles vias, connectors, cavities, launches, and radiation. The adapter must define:

- geometry simplification and meshing tolerance;
- conductor/material model and frequency dependence;
- ports and reference conductors;
- boundary conditions;
- frequency sweep/convergence criteria;
- de-embedding planes;
- solver/version and raw result retention.

[openEMS](https://docs.openems.de/intro.html) is a GPLv3 EC-FDTD solver with graded meshes,
dispersive materials, PML boundaries, frequency/time field output, and Python/Octave interfaces.
Treat it as an optional external solver adapter first; do not couple KiCad's core data model to one
solver's mesh or file format.

## 12. Accuracy, uncertainty, and reporting

Every result must identify:

- model tier and validity range;
- geometry/material parameters and source;
- typical/min/max or Monte Carlo corner;
- unresolved models and substituted defaults;
- frequency/time grid and numerical convergence;
- passivity/causality status for imported/extracted networks;
- source board/model hashes and solver version;
- comparison against a higher-fidelity or measured reference when available.

Fabrication variation should support at least copper width/thickness, dielectric height/Dk/Df,
etch profile, registration, and via dimensions. Sensitivity can be reported by finite differences:

```text
S_x = (x / y) (partial y / partial x)
```

This tells the designer whether impedance or margin is controlled by trace width, dielectric
height, material, spacing, or another parameter and is often more actionable than a nominal value.

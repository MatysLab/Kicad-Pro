# Calculation Flow

The graph separates immutable project inputs, physical extraction, reusable electrical models,
analysis engines, and user-facing results. The same extracted geometry can feed a fast analytic
model, a circuit/network solver, or an external field solver without changing the project model.

```mermaid
flowchart LR
    subgraph INPUTS[Project and model inputs]
        PCB[PCB geometry\ntracks, pads, vias, zones]
        STACK[Stackup and materials\nh, t, Dk(f), Df(f), roughness]
        NETS[Connectivity and constraints\nnets, pairs, buses, topology]
        IO[Tx/Rx/package models\nIBIS, IBIS-AMI, SPICE]
        NP[N-port models\nTouchstone and package S-parameters]
        LOAD[Power models\nVRM, current profile, capacitor RLC]
    end

    SNAP[Immutable analysis snapshot\nunits + source hashes + ports]
    PCB --> SNAP
    STACK --> SNAP
    NETS --> SNAP
    IO --> SNAP
    NP --> SNAP
    LOAD --> SNAP

    subgraph EXTRACTION[Layout-aware physical extraction]
        TOPO[Route topology\nsegments, branches, stubs, return path]
        XSEC[Cross-section sampling\nw, s, h, t, reference planes]
        VIA[Via geometry\nbarrel, pad, antipad, stub, return vias]
        PDNGEO[PDN geometry\nplanes, neck-downs, ports, mounting loops]
    end
    SNAP --> TOPO
    SNAP --> XSEC
    SNAP --> VIA
    SNAP --> PDNGEO

    subgraph ELECTRICAL[Reusable electrical models]
        RLGC[Single/multiconductor RLGC(f)\nTelegrapher equations]
        VIAMOD[Via/discontinuity model\nlumped, 2.5D, or S-parameter]
        FIELD[2D/3D field solver adapter\nC/L extraction or full-wave S(f)]
        NPORT[N-port network core\nS/Y/Z/ABCD, mixed mode, cascade]
        PDNMOD[PDN multiport Z(f)\nplanes + VRM + decaps + package]
    end
    XSEC --> RLGC
    XSEC --> FIELD
    VIA --> VIAMOD
    VIA --> FIELD
    PDNGEO --> PDNMOD
    FIELD --> NPORT
    RLGC --> NPORT
    VIAMOD --> NPORT
    NP --> NPORT

    subgraph CHANNEL[SI and channel engines]
        CHAIN[End-to-end channel\nTx package + PCB + connector + Rx package]
        XTALK[Coupled-line crosstalk\naggressor timing to victim noise]
        TRANSIENT[Time-domain response\nconvolution or circuit transient]
        STAT[Statistical response\npulse response + jitter/noise PDFs]
        AMI[IBIS-AMI Init/GetWave\nsandboxed executable process]
    end
    IO --> CHAIN
    NPORT --> CHAIN
    RLGC --> XTALK
    CHAIN --> TRANSIENT
    CHAIN --> STAT
    CHAIN --> AMI
    XTALK --> TRANSIENT
    XTALK --> STAT
    AMI --> STAT

    subgraph DDR[DDR4/DDR5 engine]
        GROUP[Recognize CK/CA/DQS/DQ groups\nand fly-by/point-to-point topology]
        DELAY[Delay and skew\ntflight = integral sqrt(L'C') dz]
        LEVEL[Training feasibility\nwrite/read leveling and gate windows]
        MARGIN[Timing budget\nsetup, hold, jitter, package, SI derating]
    end
    SNAP --> GROUP
    RLGC --> DELAY
    GROUP --> DELAY --> LEVEL --> MARGIN
    TRANSIENT --> MARGIN

    subgraph PI[Power-integrity engine]
        TARGET[Target impedance\nZtarget(f) = allowed ripple / current spectrum]
        ZSWEEP[Multiport impedance sweep\nZii and Zij vs frequency]
        RES[Resonance/anti-resonance\npeaks, Q, mode shape]
        OPT[Decoupling optimization\npart, count, placement, mounting L]
    end
    LOAD --> TARGET
    PDNMOD --> ZSWEEP
    TARGET --> ZSWEEP --> RES --> OPT

    subgraph RESULTS[Results and design feedback]
        MAP[Layout overlays\nimpedance, delay, coupling, current density]
        EYE[Eye, bathtub, BER contour\nwith confidence and assumptions]
        DDRR[DDR timing table\nper byte/nibble/device margins]
        PDNR[PDN impedance and optimized BOM\npeak attribution and what-if comparison]
        REPORT[Reproducible report\ninputs, hashes, solver, convergence, warnings]
    end
    RLGC --> MAP
    XTALK --> MAP
    TRANSIENT --> EYE
    STAT --> EYE
    MARGIN --> DDRR
    OPT --> PDNR
    MAP --> REPORT
    EYE --> REPORT
    DDRR --> REPORT
    PDNR --> REPORT
```

## Minimum calculation chain for a trustworthy result

1. Resolve ports and the actual return path from the PCB.
2. Sample cross-sections along each route; do not assume a single width or dielectric height.
3. Extract frequency-dependent RLGC or an N-port model at each discontinuity.
4. Assemble and validate the network (reference impedance, passivity, causality, bandwidth).
5. Apply source, package, receiver, jitter/noise, and protocol timing models.
6. Produce the result together with its validity range, unresolved-model warnings, and
   convergence/correlation data.


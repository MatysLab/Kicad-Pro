# KiCad Pro

KiCad Pro is an engineering-focused fork of [KiCad](https://www.kicad.org/) that extends the
PCB design workflow with practical, production-oriented tools.

The project follows the KiCad codebase while developing additional capabilities for board
definition, manufacturing preparation, and electrical design. It is independently maintained
and is not an official KiCad release.

## Via Stitching

KiCad Pro adds a professional Via Stitching tool to the PCB Editor. It is available from both the
top toolbar and the **Place** menu and places the complete result as one undoable board operation.

- Stitch inside selected copper zones or polygon shapes, or across matching zones on checked
  copper layers.
- Select the connection net, defaulting to `GND` when that net exists.
- Set manual center-to-center spacing, with a 10 mm default.
- Use RF spacing derived from frequency and effective dielectric constant at λg/10 or λg/20.
- Configure via diameter and drill using the board's current via settings as defaults.
- Avoid pads, existing tracks and vias, polygon edges, and via keepout areas.
- Limit each operation to 10,000 vias to keep placement responsive and reviewable.

The implementation is split between the
[Via Stitching dialog](pcbnew/dialogs/dialog_via_stitching.cpp),
[placement engine](pcbnew/tools/drawing_tool.cpp),
[top toolbar](pcbnew/toolbars_pcb_editor.cpp), and
[Place menu](pcbnew/menubar_pcb_editor.cpp).

## Controlled-impedance stackups

The Board Setup **Physical Stackup** editor includes an integrated controlled-impedance workflow:

- Enable impedance control directly beside the copper-layer and dielectric controls.
- Choose a manufacturer stackup preset to populate the complete physical construction.
- Set a target impedance independently for every copper layer.
- Select microstrip, stripline, coplanar, or differential trace structures.
- Calculate trace widths automatically whenever the target or stackup changes.
- Continue editing copper thickness, dielectric thickness, material, and dielectric constant
  manually when a custom construction is required.

Included presets cover the published controlled-impedance constructions from:

- [JLCPCB](https://jlcpcb.com/impedance)
- [PCBWay](https://www.pcbway.com/multi-layer-laminated-structure.html)

Preset values are design inputs, not manufacturing guarantees. Confirm the final construction,
material properties, copper weight, and impedance requirements with the selected fabricator before
releasing a board for production.

The selected preset and controlled-impedance settings are stored in the `.kicad_pcb` file. This
includes the imported preset definition, enabled state, and each copper layer's trace structure,
target impedance, and spacing. Reopening or sharing the board therefore restores the same setup
without requiring the original preset file.

### Importing a stackup preset

Select **Import...** beside **Stackup preset** and choose a UTF-8 JSON file using the following
versioned format. All thickness values are expressed in millimetres. `dielectrics` must contain one
array for every space between adjacent copper layers; each array can contain one or more core or
prepreg sublayers.

```json
{
  "format": "kicad-pro-stackup",
  "version": 1,
  "manufacturer": "Example Fab",
  "name": "EX0416-7628",
  "copper_thickness_mm": [0.035, 0.0175, 0.0175, 0.035],
  "dielectrics": [
    [
      {
        "type": "prepreg",
        "material": "7628",
        "thickness_mm": 0.2104,
        "epsilon_r": 4.4,
        "loss_tangent": 0.02
      }
    ],
    [
      {
        "type": "core",
        "material": "FR-4 core",
        "thickness_mm": 1.065,
        "epsilon_r": 4.6,
        "loss_tangent": 0.02
      }
    ],
    [
      {
        "type": "prepreg",
        "material": "7628",
        "thickness_mm": 0.2104,
        "epsilon_r": 4.4,
        "loss_tangent": 0.02
      }
    ]
  ]
}
```

Required fields are `format`, `version`, `manufacturer`, `name`, `copper_thickness_mm`, and
`dielectrics`. Every dielectric sublayer requires `type`, `material`, `thickness_mm`, and
`epsilon_r`; `loss_tangent` is optional and defaults to `0.02`. The copper-layer count must be an
even number from 2 through 32, and the number of dielectric arrays must be exactly one less than the
copper-layer count.

## Building

KiCad Pro uses the same toolchain and platform requirements as upstream KiCad. See the official
[KiCad build documentation](https://dev-docs.kicad.org/en/build/) for dependency installation and
complete platform-specific instructions.

A typical local build uses CMake and Ninja:

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
```

To rebuild only the PCB editor module while developing stackup features:

```sh
cmake --build build/release --target pcbnew_kiface
```

## Testing changes

Before submitting a change, build the affected targets and run the relevant tests from the `qa`
directory. For user-interface work, launch the locally built application and verify the complete
workflow with a representative board.

Useful project references:

- [KiCad developer documentation](https://dev-docs.kicad.org/)
- [KiCad contribution guide](https://dev-docs.kicad.org/en/contribute/)
- [KiCad source repository](https://gitlab.com/kicad/code/kicad)
- [KiCad community forum](https://forum.kicad.info/)

## Repository layout

| Path | Purpose |
| --- | --- |
| `pcbnew/` | PCB editor and board-setup implementation |
| `eeschema/` | Schematic editor |
| `kicad/` | Project manager |
| `common/` | Shared application code |
| `libs/` | Shared geometry and utility libraries |
| `qa/` | Automated tests and test data |
| `resources/` | Application resources and project templates |
| `thirdparty/` | Bundled third-party dependencies |

## Contributing

Keep changes focused, preserve compatibility with upstream file formats, and include validation
appropriate to the affected subsystem. Bug reports and pull requests should describe the board
configuration, expected result, actual result, and reproduction steps.

## License and attribution

KiCad Pro retains KiCad's licensing and contributor history. See [LICENSE](LICENSE) and
[AUTHORS.txt](AUTHORS.txt) for details.

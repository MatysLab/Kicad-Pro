# KiCad Pro

KiCad Pro is an engineering-focused fork of [KiCad](https://www.kicad.org/) that extends the
PCB design workflow with practical, production-oriented tools.

The project follows the KiCad codebase while developing additional capabilities for board
definition, manufacturing preparation, and electrical design. It is independently maintained
and is not an official KiCad release.

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

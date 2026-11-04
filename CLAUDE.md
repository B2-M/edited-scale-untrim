# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## What this is

ScaleUntrim converts CAD models or triangle meshes into quad meshes, multi-patch quad layouts and NURBS surfaces. Its executable is called `QuadriFlow`. In the parent project `sweeps_base_quad_meshing`, where this repository is cloned into `deps/ScaleUntrim`, its job is to provide robust quad layouts on **planar** base surfaces, which are then swept into 3D. Base surfaces may contain holes.

`main` is based on the paper's Zenodo release (`IMS-XWei-ScaleUntrim-c47b41e`, "Scalable Field-Aligned Reparameterization for Trimmed NURBS"), imported in commit `4e58b58`. The commits after it add Linux support, the scripted interface and robustness fixes. To see only those changes, run `git diff 4e58b58 main`.

## Remotes

- `myfork` (`B2-M/edited-scale-untrim`): its `main` is what `scripts/scaleUntrimBuilder.py` in the parent project clones. Push `main` here with `git push myfork main`.
- `origin` (`kendrickshepherd/edited-scale-untrim`): `origin/main` is still the pre-merge code at `9ce0591`. Local `main` tracks it, so a plain `git push` from `main` targets the wrong remote.

## Build and run

Requires OpenCASCADE, Boost and Eigen.

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4 QuadriFlow
./QuadriFlow <input.obj> ../setting.config [output.step] [--sharp | --no-sharp]
```

`setting.config` must have `temp_dir` set to an absolute path ending in `/`, because file names are appended to it directly. All output goes there.

With `run_from: 2`, the run reads the given OBJ, writes `quad.vtk`, then `patch.txt` and `patchN.vtk`, and stops. NURBS fitting and STEP export are done with `run_from: 3`.

| Exit code | Meaning |
|---|---|
| 0 | Finished. This does **not** prove the layout is valid; see below. |
| 3 | Clean failure, with the reason on stderr. `quad.vtk` exists if the failure came after it was written. |
| other | Crash. |

The memory limit is 8 GB of address space. `QUADRIFLOW_MAX_MEMORY_GB` overrides it and `0` disables it. It is skipped under AddressSanitizer.

## `setting.config`

Parsed in `MergeFit::read_config_file` (`mergefit/source/merge_fit.cpp`), with defaults in the constructor. Values are read with plain `fin >> value`, so an empty value swallows the next key and silently shifts everything after it. The committed file has an empty `temp_dir:`, which `writeConfigFile()` in `scaleUntrimBuilder.py` fills in.

| Key | Effect |
|---|---|
| `temp_dir` | Output directory. Required. |
| `run_from` | Pipeline start: 0 CAD, 1 triangle mesh, 2 fixed triangle mesh, 3 `patch.txt`, 4 STEP only. |
| `magnitude_factor` | Target quad size scaling. Values of 0 or below use the built-in 1.4. |
| `quad_face_num` | **Overrides `magnitude_factor` unless it is 0.** Above 0 is a target quad count; below 0 uses `sqrt(area / vertex count)`. |
| `magnitude` | When 1, scales quad size by input face count for large meshes (10k faces and up). |
| `collapsing_layer` | At 1 the chain collapse in `size_n` is skipped. Above 1 runs it, giving fewer patches but more invalid layouts. |
| `minimum_cost` | Use the minimum-cost-flow solver for the integer constraints. |
| `preserve_sharp`, `angle_for_sharp` | Sharp-feature preservation and its angle. The angle also sets the boundary-corner threshold, and 60 reproduces the original value. `--sharp`/`--no-sharp` override `preserve_sharp`. |
| `fix_hole`, `local_layer`, `smooth_iteration` | Mesh fixing. Only used at `run_from` 0 and 1. |
| `fit_tolerance` | NURBS fitting tolerance. |
| `area_tolerance` | Only printed for comparison. |
| `perform_mirror` | No effect: both branches set it to false. |

## Known limitations

Measured with AddressSanitizer on 14 planar meshes at `magnitude_factor` 1.0 to 2.0 in steps of 0.1 (154 runs), one thread, `collapsing_layer: 1`, at commit `422c608`: 88 valid layouts, 24 invalid, 42 clean failures or no output, and no crashes or hangs. Validity meant no uncovered faces, area error within 2%, no flipped or overlapping cells, and patches joining without gaps.

- **Domains with holes are not properly supported.** The no-singularity case in `extract_patch` assumes a rectangle with four boundary sides, and an annulus has none. It got 0 of 11 valid; a plate with a hole got 2 of 11.
- **The rotor cross-sections** in the parent project's `src/tempDir/Loop1` and `Loop2` got only 1 of 11 valid each.
- **Patches can lose cells that the quad mesh covers.** In 18 of 286 runs on 14 planar meshes, `quad.vtk` was fine but the patches were not. All cells were still covered after the first grids were built in 14 of them, so the chain collapse lost the rest (6 in the `sin_*` edits, 8 in `sin_mismatching`). In the other 4, cells were already missing from the first grids. The causes are unknown.
- **`quad.vtk` itself can be wrong.** In 19 of those runs it lost area or filled a hole (`plate_hole`, exactly the hole's area). `generateQuadMesh` in the parent project does not check this.
- **Open failure causes**, as reported on stderr:
  - chain collapse in `sin_mismatching` and `extract_patch` finding no matching patch or layer (these and the rest of the chain collapse index `collapse_patch` and `relative_point` without checks, so other inputs still segfault or read freed memory, for example at the `collapse_patch` reads after `cannot_collapse` is declared)
  - no singularities and fewer than four boundary sides
  - corner index out of range in `optimize_positions_dynamic`
  - solver NaN, which exits 0 without a `patch.txt`
- **NURBS refinement can diverge.** With `run_from: 3`, 87 of 88 valid layouts fit in one iteration with the right area. On `rotor_loop2` at factor 1.1 the first fitting error (0.014) is above `fit_tolerance`, so the fit refines, and after four iterations the STEP shell has 322 times the input's area, still with exit code 0. A `fit_tolerance` above the first error avoids this, and `toStep` in the parent project checks the area. The cause of the divergence is unknown.
- **Coarse inputs fail more often.** Quad count follows input density. One midpoint subdivision helps on coarse meshes (`hook_x0` went from 1 to 8 valid factors of 11, `disc_coarse` from 2 to 10), but not on the rotors, where it lowered the valid count from 1 to 0, and a second one gave no further gain. `generatePatchLayout` in the parent project therefore tries both densities.
- **Results are chaotic in the input.** Two hook meshes differing by at most 3.6e-15 gave 10 and 8 valid factors, so validate every result rather than relying on a factor that worked before.
- **Subdividing exposes more failures.** On subdivided meshes with holes and on the rotors, Release runs also segfaulted (5 runs) and timed out at 240 s (3 runs). A run on `disc_fine` subdivided once exited 0 with point id 32766 in `patch.txt`, which has 3293 points.
- **Quality varies by factor.** A valid layout at one `magnitude_factor` says little about the next one, so try several and validate each result.

`extract_patch` keeps parallel vectors index-aligned by convention only, which is the common source of its bugs.

## Testing

`validatePatchLayout` in `scripts/patchLayout.py` of the parent project checks a layout on a planar mesh in any plane, and is what `generatePatchLayout` uses. `scripts/island_check.py` there is a simpler coverage check for the XY plane.

Run experiments under a memory cap, because a run must never be able to exhaust the machine. The limit in `main()` does not apply to ASan builds, and uncapped ASan runs have crashed the desktop session.

```bash
systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 \
    nice -n 19 timeout -k 5 240 ./QuadriFlow ...
```

Run at most about three at once. Pin `OMP_NUM_THREADS=1` for reproducible comparisons, because `source/hierarchy.cpp` calls the thread-unsafe `rand()` inside OpenMP loops.

ASan builds return exit code 0 after a clean failure while Release returns 3, with the same source. The cause is unknown, so check the log for `QuadriFlow failed:` when using ASan.

`pcg32::shuffle` has an early return that disables shuffling. It comes from the Zenodo release and is kept so quad meshes match it.

`test/` (built with `-DBUILD_ISLAND_TESTS=ON`) checks frozen outputs in `test/fixtures/`. **These predate the Zenodo merge and do not run any QuadriFlow code**, so they do not test `main` and cannot catch crashes. They need regenerating, or replacing with live runs.

## History

The earlier line of work is at `9ce0591`, which is `origin/main`. It fixed eight bugs in `extract_patch`, `collapse_boundary`, `sin_mismatching` and `match_patch`. Those fixes are **not** on `main`: ported to the Zenodo code they gave one-face islands and STEP export aborts on the disc, and enabling the `collapse_boundary` call, which the Zenodo code never makes, caused area losses of up to 56%. The full write-up is in the git history of the parent project's `CLAUDE.md`.

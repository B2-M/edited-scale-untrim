# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## What this is

ScaleUntrim converts CAD models or triangle meshes into quad meshes, multi-patch quad layouts and NURBS surfaces. Its executable is called `QuadriFlow`. In the parent project `sweeps_base_quad_meshing`, where this repository is cloned into `deps/ScaleUntrim`, its job is to provide robust quad layouts on **planar** base surfaces, which are then swept into 3D. Base surfaces may contain holes.

`main` is based on the paper's Zenodo release (`IMS-XWei-ScaleUntrim-c47b41e`, "Scalable Field-Aligned Reparameterization for Trimmed NURBS"), imported in commit `4e58b58`. The commits after it add Linux support, the scripted interface and robustness fixes. To see only those changes, run `git diff 4e58b58 main`.

**See [FINDINGS.md](FINDINGS.md)** for the bugs found, the design decisions, dead ends and open problems, and `TestingPatchLayouts.md` in the parent project for how to test a change.

## Remotes

- `myfork` (`B2-M/edited-scale-untrim`): its `main` is what `scripts/scaleUntrimBuilder.py` in the parent project clones. Push `main` here with `git push myfork main`.
- `origin` (`kendrickshepherd/edited-scale-untrim`): `origin/main` is still the pre-merge code at `9ce0591`. Local `main` tracks it, so a plain `git push` from `main` targets the wrong remote.

The robustness work is on `merge_zenodo_paper`. Check with `git log myfork/main..merge_zenodo_paper` that it has reached `myfork/main`, because the parent project's builder deletes this folder and clones that branch, which would silently lose whatever is not there.

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
| `collapsing_layer` | At 1 only the chain collapse in `size_n` is skipped, while the `sin_*` steps still run. Above 1 runs it, giving fewer patches but more invalid layouts. |
| `minimum_cost` | Use the minimum-cost-flow solver for the integer constraints. |
| `preserve_sharp`, `angle_for_sharp` | Sharp-feature preservation and its angle. The angle also sets the boundary-corner threshold, and 60 reproduces the original value. `--sharp`/`--no-sharp` override `preserve_sharp`. |
| `fix_hole`, `local_layer`, `smooth_iteration` | Mesh fixing. Only used at `run_from` 0 and 1. |
| `fit_tolerance` | NURBS fitting tolerance, relative to the diagonal of the part. Refinement continues while the error is above it. |
| `area_tolerance` | Only printed for comparison. |
| `perform_mirror` | No effect: both branches set it to false. |

## Known limitations

Measured on 18 planar meshes at `magnitude_factor` 1.0 to 2.0 in steps of 0.1, as given and subdivided once where under 10,000 triangles (363 Release runs), one thread, `collapsing_layer: 1`: 250 valid layouts, 83 invalid, 30 failed. Every mesh has a valid layout that passes STEP export. A few runs differ between binaries or output paths, because of the remaining memory errors. Validity means no uncovered faces, area error within 2%, no flipped or overlapping cells, and patches joining without gaps.

- **Layouts with several holes are valid but fragmented.** `disc_2holes` has 93 patches on 383 cells and `plate_3holes` 42, while `plate_hole` has 8 and `plate_2holes` 19.
- **Rings need a regular mesh to get a simple layout.** A regular quad mesh of a domain with one hole has no corners, so `split_ring_into_patches` cuts it into two patches along mesh lines. An annulus mesh with irregular vertices goes through normal patch extraction instead, and 7 of 22 annulus runs are invalid.
- **Thin parts have few usable factors.** `rotor_loop1` is valid in 6 of 22 runs and `rotor_loop2` in 3 of 22. At coarse factors `rotor_loop2` becomes a strip one cell wide, or loses 65% to 77% of its area for an unknown reason. Long cell edges can cut across a curved thin tail.
- **The chain collapse can lose cells or fail.** On planar meshes `extract_patch` therefore keeps the last layout that covers all faces, which has more patches. Why it loses cells is unknown. In some runs the first grids already miss cells, so nothing can be kept. Other surfaces have no such fallback.
- **The chain collapse has memory errors.** It indexes `collapse_patch` and `relative_point` without checks, so some inputs segfault or read freed memory, and these cannot be caught.
- **Open failure causes**, as reported on stderr:
  - corner index out of range in `optimize_positions_dynamic`
  - chain collapse in `sin_mismatching` and `extract_patch` finding no matching patch or layer, where no complete layout could be kept
  - no singularities and fewer than four boundary sides, for a mesh that is not a regular ring
  - patch not a rectangular grid in `Outputpatch`
  - solver NaN, which exits 0 without a `patch.txt`
- **Exit code 0 can come with bad data,** such as a point id that does not exist or infinite coordinates in `patch.txt`, so always validate.
- **NURBS refinement can diverge.** `MultiPatchFit::run` therefore keeps the best fit instead of the last one. The cause is unknown, and a tighter `fit_tolerance` can also make the area worse.
- **Coarse inputs work less often.** Quad count follows input density, so `generatePatchLayout` in the parent project tries each mesh as given and subdivided once. A second subdivision gave no further gain.
- **Results are chaotic in the input.** Two hook meshes differing by at most 3.6e-15 gave 10 and 8 valid factors, so try several factors and validate each result rather than relying on one that worked before.
- **STEP files have not been opened in a CAD system.** Only the number of faces and the total area were checked.

`extract_patch` keeps parallel vectors index-aligned by convention only, which is the common source of its bugs.

## Testing

Follow `TestingPatchLayouts.md` in the parent project: `scripts/runPlanarLayoutSweep.py` there runs all test meshes and compares each run with a baseline. `validatePatchLayout` in `scripts/patchLayout.py` checks a layout on a planar mesh in any plane. `scripts/island_check.py` is a simpler coverage check for the XY plane.

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

# Findings on robust planar layouts

What was learned while making ScaleUntrim produce robust coarse layouts on planar geometries, in November 2026. `CLAUDE.md` holds the current state. This file holds the evidence and the reasoning, so that dead ends are not repeated. How to run everything is in `TestingPatchLayouts.md` of the parent project.

## Goal

There are two applications with different needs:

- **2D:** the coarse patches are the target. The result should be a usable geometry file, such as STEP.
- **3D:** the quad mesh is the input of a sweep, and `fitHexMeshToSweep` builds its own patches from it. Patch validity does not matter there, but the minimum Jacobian does.

Domains may have holes. Extending the sweep to bases with holes is deliberately deferred until the 2D workflow is robust.

## Choice of code base

The earlier line of work (`9ce0591`) and the paper's Zenodo release (`IMS-XWei-ScaleUntrim-c47b41e`) were compared on 154 runs of 14 planar meshes. The Zenodo base with minimal fixes gave more valid layouts than the old repository (88 against 59), so it became `main`. It is not proven that Zenodo is a later revision of the same code. Porting the old fixes gave one-face islands, and enabling `collapse_boundary` caused area losses of up to 56%.

## Results over time

All counts are from one thread, at factors 1.0 to 2.0 in steps of 0.1. From the subdivision step on, each mesh also runs subdivided once, which gives 286 runs on the first 14 meshes.

| Step | Runs | Valid | Invalid | Failed |
|---|---:|---:|---:|---:|
| Old repository | 154 | 59 | 52 | 43 |
| Zenodo base, before safeguards | 154 | 88 | 23 | 43 |
| Clean failures instead of crashes and hangs | 154 | 88 | 24 | 42 |
| Subdivision added | 286 | 167 | 52 | 67 |
| Boundary lookup fixes | 286 | 171 | 75 | 40 |
| Selective face deletion | 286 | 176 | 67 | 43 |
| Coverage fallback | 286 | 194 | 58 | 34 |
| `FixHoles` keeps real boundaries | 286 | 201 | 59 | 26 |
| Ring splitting | 286 | 212 | 59 | 15 |
| Best NURBS fit, last sweep | 286 | 213 | 58 | 15 |

A rerun of the same code in another output folder gave 211, which shows the size of the noise. With four multi-hole meshes added, the baseline is 250 valid, 83 invalid and 30 failed of 363 runs. Every one of the 18 meshes has a valid layout whose default STEP export passes its checks.

## Bugs found and fixed

| Symptom | Cause | Fix | Evidence |
|---|---|---|---|
| `unordered_map::at` or a range check, on rotors | `E2E_compact` keeps entries for faces removed at the start of `extract_patch`, so its indices no longer match `F_compact` (808 entries for 188 faces) | Find boundary edges from the current faces | Lookup errors 20 → 0, chain walk errors 20 → 0, all valid layouts identical |
| Same errors on `plate_hole`, annulus | `FixValence` splits a boundary vertex, and a copy surrounded by faces inherits the boundary flag through `Vset`, staying pinned on top of the original | Only flag vertices with a boundary edge | The failing vertex had 0 boundary edges, was the last index, and had a neighbour at distance 0 |
| Blade tips missing, about −10% area, in `quad.vtk` too | `optimize_positions_dynamic` deleted every face with four constrained vertices, which includes normal cells across a thin part | Only delete flipped or very small faces (below 0.1 × target cell size²) | `rotor_loop1` 1 → 6 valid, best layout 12 → 5 patches |
| Regions missing from patches although `quad.vtk` covers them | The chain collapse loses cells | Keep the last layout that covers all faces, on planar meshes | 18 runs rescued, all export to STEP. Cells were first lost in the first grids in 4 of 18 runs, in the `sin_*` edits in 6, and in `sin_mismatching` in 8 |
| Filled holes at exactly +4.89% (`plate_hole`); meshes collapsed to about −99% with Euler characteristic 2 (`disc_coarse`, `hook_x0`) | `FixHoles` caps every boundary loop under 25 vertices. This makes sense for closed surfaces, but caps real boundaries of open domains | Keep loops that mostly lie on the input's boundary | The hole's loop has 21 vertices at factor 1.4 and 32 at 1.0, which is why 1.0 always worked. `plate_hole` best layout 13 → 8 patches |
| "No singularities and fewer than four boundary sides" on the annulus | A regular quad mesh of a ring has no corners, so tracing has no start | `split_ring_into_patches` cuts it along mesh lines | 11 of 12 failing runs were perfect structured rings. Annulus 2 → 13 of 22, best layout 25 → 2 patches |
| STEP area 322 times too large on `rotor_loop2`, exit code 0 | The NURBS refinement keeps the last iteration, even a diverged one | Keep the best fit | Fitting errors 0.014, 0.014, 0.014, 0.34 |
| Heap errors in `sin_mismatching` | A typo (`find(n_c.begin(), need_collapse.end(), i)`) and a missing size check | Fixed | Found with AddressSanitizer |

## Design decisions

- **Cuts for rings are automatic.** A user step was considered as a fallback, but it was not needed: a regular mesh on a ring is a structured grid. A user-defined cut could still become useful for multi-hole domains, which currently have many patches.
- **Two patches per ring.** Two, three and four all gave valid layouts on five test runs, and two had the smallest STEP area error (0.02%). The two patches share two sides, and `multiPatchDecomposition` in the sweep code records only one connection per patch pair, which will matter if the sweep is extended to holes.
- **Selective deletion instead of none.** Removing the face deletion altogether lost 3 valid layouts, because it also removes real slivers. Only the threshold 0.1 was tested.
- **The fallback only applies to planar meshes.** Coverage can only be checked in the plane. Other surfaces behave as before.
- **One subdivision, up to 10,000 triangles.** A second one gave no fewer patches on any mesh, cost up to 758 s and made `disc` worse. After the `FixHoles` fix the best layout comes from the original density on 16 of 18 meshes, but subdivision still gives the best layout on two multi-hole meshes and more valid factors on coarse ones.
- **Selection is fewest patches, then fewest cells, then area error.** This puts some area errors near the 2% limit (`disc_coarse` −1.98%).
- **`fit_tolerance` is not loosened automatically**, so that a poor fit is not hidden. The fitting error is relative to the part's diagonal.
- **One-sided edges along a curved boundary stay invalid.** On `rotor_loop1` a long straight cell edge cuts across the curved tail, with about 1% area lost. This is a real geometric error, and other layouts exist.
- **`generateQuadMesh` keeps its own path.** Going through patches for 3D would make it depend on patch extraction. On the hook, 6 of 10 factors swept with positive Jacobians from `quad.vtk`, but only 2 through a patch-derived OBJ.
- **`quadMeshAreaError` only applies to planar bases**, because a coarse mesh of a curved surface has less area without being wrong.

## Wrong turns worth remembering

- "Patches that do not join" was the wrong diagnosis at first. Plots and stage-by-stage areas showed that in many runs the quad mesh was already wrong, and that the missing areas were not joining problems.
- The first `FixHoles` fix had no effect, because the parametrizer's `E2E` and `F` are empty at that point. The right data is `Boundary_vertices`.
- The first explanation for the NURBS divergence, that an absolute tolerance was too large for a small part, was wrong: the tolerance is relative. A smaller tolerance changed nothing, and a larger one only avoided the refinement.
- `patch.txt` and `patchN.vtk` looked inconsistent in 25 of 111 runs. They are identical: the check sorted `patch10` before `patch2`.
- The label "dropped patches" was too broad: half of those runs lose only one to three cells.
- A closed ring having no start corner was suspected of losing the annulus's inner ring. That run actually lost its cells in the `sin_*` edits, and the missing ring in the picture is unexplained.

## Other observations

- **Results are chaotic.** Two hook meshes differing by at most 3.6e-15 gave 10 and 8 valid factors, so a factor that worked before says little.
- **Some results depend on memory layout.** The remaining memory errors make a few runs differ between binaries or output paths: `plate_hole` subdivided at 1.6 crashed reliably with one binary and passed with another, without a relevant logic change.
- **A valid 2D mesh can still invert in the sweep.** All nine hook quad meshes were convex with correct orientation, but three had a negative minimum Jacobian.
- **Patch points are not `quad.vtk` vertices.** `patch.txt` has its own, smaller index space, and neighbouring patches can use different ids at the same location (in 21 of 88 valid layouts), so exports must weld them.
- **Invalid data can exit with code 0:** point id 32766 in a `patch.txt` of 3293 points, infinite coordinates, and an empty `patch.txt`.

## Open problems

- **Multi-hole layouts are fragmented.** `disc_2holes` has 93 patches on 383 cells and `plate_3holes` 42. Merging neighbouring patches that form a rectangle is the most promising next improvement.
- **STEP files have not been opened in a CAD system.** Only the face count and total area are checked.
- **Chain collapse memory errors remain,** with segfaults on a few runs, a use-after-free at the `collapse_patch` reads after `cannot_collapse`, and a heap overflow in `sin_mismatching`. Their data model of `collapse_patch` entries is fragile, so guarding each read was judged not worthwhile, as these runs would still fail.
- **Why the chain collapse loses cells** is unknown, as are the 4 runs where the first grids already miss cells.
- **Rotors have few usable factors** (`rotor_loop1` 6 of 22, `rotor_loop2` 3 of 22). At coarse factors `rotor_loop2` becomes a strip one cell wide (19 cells, against 155 at factor 1.1), or its boundary collapses into long thin cells with 65% to 77% area lost. The cause of the sudden drop in cell count is unknown.
- **Inverted hexes in 3D are not checked** by `generateQuadMesh`, and the minimum Jacobian is not available in Python.
- **ASan builds return 0 after a clean failure** while Release returns 3, for an unknown reason.
- **The cause of the NURBS divergence** is unknown, and a tighter tolerance can also make the area worse on `rotor_loop1`.

// Regression tests for Optimizer::extract_patch's "island" bug family:
// quad-mesh faces silently assigned to no patch at all in the final
// decomposition (as opposed to the earlier, separately-fixed family of
// crashes and degenerate-sliver patches - see CLAUDE.md's "Patch-extraction
// bug fixes" section at the project root for the full history).
//
// These tests run against FROZEN FIXTURES (test/fixtures/disc_mf<X>/), not
// a live QuadriFlow invocation:
//   - QuadriFlow's field solver is not fully deterministic between runs for
//     every magnitude_factor value (confirmed for 1.0 during the
//     investigation that led to these tests: distinct singularity
//     count/placement across repeated runs). A frozen fixture makes each
//     test reproducible regardless of that.
//   - A live run takes 30-90s of solver time per magnitude_factor value;
//     these tests run in milliseconds against the frozen output.
//   - The fixtures were captured directly from real QuadriFlow runs on
//     disc.obj (not hand-constructed), so they exercise the actual
//     extract_patch code path's real output shape, not an approximation
//     of it.
//
// Regenerating a fixture (e.g. after a genuine, intentional change to
// extract_patch's output): run
//   ./QuadriFlow <path>/disc.obj <config with temp_dir pointing at the
//     fixture directory, run_from: 2, the matching magnitude_factor>
// and overwrite that fixture directory's quad.vtk/patchN.vtk. Do not
// regenerate a fixture just because a test fails - that would silently
// bless a regression; investigate first.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "IslandCoverageCheck.hpp"

namespace {

// Fixture root is this test file's own directory (test/fixtures/), resolved
// via the CMake-provided FIXTURE_DIR macro (see test/CMakeLists.txt) rather
// than a relative path, since ctest's working directory is the build
// directory, not the source tree.
std::string fixturePath(const std::string& caseDir, const std::string& file) {
    return std::string(FIXTURE_DIR) + "/" + caseDir + "/" + file;
}

std::vector<std::string> patchPaths(const std::string& caseDir, int nPatches) {
    std::vector<std::string> paths;
    for (int i = 0; i < nPatches; ++i) {
        paths.push_back(fixturePath(caseDir, "patch" + std::to_string(i) + ".vtk"));
    }
    return paths;
}

// Ordinary discretization noise between quad.vtk's own triangulation and
// the patch grid's (coarser, differently-subdivided) triangulation of the
// same region. Observed range on known-clean fixtures: 0.04-0.57% (1.2:
// 0.043%, 1.4 post-fix: -0.026%, 1.8: 0.126%, 2.0: 0.565%). A real island
// is a full order of magnitude past that (0.871% pre-fix at 1.4, 2.327% at
// 1.6); this bound sits above the observed clean range (with margin, since
// a different disc discretization could legitimately land a bit higher)
// and comfortably below either known island's magnitude - it would catch
// 1.4's original bug too if that fixture were swapped for the pre-fix one.
constexpr double kCleanAreaDiffBound = 0.01;  // 1%

}  // namespace

TEST_CASE("disc magnitude_factor 1.2 has a clean, fully-covering decomposition", "[island_coverage]") {
    auto result = island_check::checkCoverage(fixturePath("disc_mf1.2", "quad.vtk"), patchPaths("disc_mf1.2", 5));

    CHECK(result.n_quad_faces == 268);
    CHECK(result.n_patch_faces == 240);
    CHECK(result.islands.empty());
    CHECK(std::fabs(result.area_diff_fraction) < kCleanAreaDiffBound);
}

TEST_CASE("disc magnitude_factor 1.4 (pre-fix fixture) documents the ORIGINAL bug", "[island_coverage][historical]") {
    // Captured from the source as it stood at commit 5ef430b (the commit
    // immediately before the fix in 3be759b), via a temporary git checkout
    // of just source/optimizer.cpp back to that commit, rebuild, run,
    // restore - see this test's sibling below for what the fix changed.
    // This fixture exists so the before/after contrast is reproducible and
    // checked in, not just asserted in a commit message; it is not a
    // "should pass" test in the usual sense, it is a frozen record of the
    // bug for comparison. If this ever starts reporting 0 islands, the
    // fixture was regenerated against the wrong binary - re-verify against
    // commit 5ef430b specifically, don't just accept the new number.
    auto result =
        island_check::checkCoverage(fixturePath("disc_mf1.4_prefix", "quad.vtk"), patchPaths("disc_mf1.4_prefix", 6));

    CHECK(result.n_quad_faces == 222);
    CHECK(result.n_patch_faces == 123);
    CHECK(result.islands.size() == 3);
    CHECK(result.area_diff_fraction > 0.005);  // 0.871% observed; the bug this repo fixed
}

TEST_CASE("disc magnitude_factor 1.4 (post-fix) has a clean, fully-covering decomposition",
          "[island_coverage][regression]") {
    // Documents the fix in commit 3be759b (deps/ScaleUntrim): a closed-loop
    // chain's shared "seam" patch was dropped with no replacement built for
    // it (3 of 222 faces uncovered pre-fix), then, once kept, corrupted into
    // a near-zero-area sliver by position-interpolation math that assumed
    // every patch in a pair was one slice of that pair's single continuous
    // boundary strip. Both are fixed; this must stay 0 islands. Compare
    // against the sibling test above, which pins the pre-fix numbers this
    // fix changed: n_patch_faces 123->124, islands 3->0, area_diff_fraction
    // 0.871%->-0.026%.
    auto result =
        island_check::checkCoverage(fixturePath("disc_mf1.4_postfix", "quad.vtk"), patchPaths("disc_mf1.4_postfix", 7));

    CHECK(result.n_quad_faces == 222);
    CHECK(result.n_patch_faces == 124);
    CHECK(result.islands.empty());
    CHECK(std::fabs(result.area_diff_fraction) < kCleanAreaDiffBound);
}

TEST_CASE("disc magnitude_factor 1.6 still has a known, NOT YET FIXED island", "[island_coverage][known_issue]") {
    // This is an XFAIL-shaped test: it documents and pins the CURRENT buggy
    // behavior so a future, unrelated change doesn't silently alter it
    // without notice, and so fixing it is a visible, celebrated event (this
    // test starts failing/changing shape) rather than something that has to
    // be independently remembered.
    //
    // Root cause (traced, not yet fixed - see CLAUDE.md): a different
    // trigger than 1.4's. An open (non-closed-loop) chain's two boundary-end
    // patches (outside both of its own transition pairs) get pushed into
    // sin_mismatching's exclusion list (Vect) unconditionally via
    // following_collapse, but a replacement is only ever built for a
    // following_collapse entry that also repeats across a DIFFERENT chain
    // (double_following/can). With only one chain present in this run,
    // nothing repeats, no replacement is built, and both patches - plus the
    // two patches collapsed in the chain's own pairs - vanish with nothing
    // to replace them.
    //
    // When this gets fixed: change the CHECK(result.islands.size() == 4) to
    // CHECK(result.islands.empty()), regenerate this fixture against the
    // fixed binary, move this test's tag from [known_issue] to
    // [regression], and update CLAUDE.md's "Known open issue" section.
    auto result = island_check::checkCoverage(fixturePath("disc_mf1.6", "quad.vtk"), patchPaths("disc_mf1.6", 5));

    CHECK(result.n_quad_faces == 171);
    CHECK(result.n_patch_faces == 96);
    CHECK(result.islands.size() == 4);
    CHECK(result.area_diff_fraction > 0.02);  // ~2.3% observed; well outside clean noise
}

TEST_CASE("disc magnitude_factor 1.8 has a clean, fully-covering decomposition", "[island_coverage]") {
    auto result = island_check::checkCoverage(fixturePath("disc_mf1.8", "quad.vtk"), patchPaths("disc_mf1.8", 5));

    CHECK(result.n_quad_faces == 119);
    CHECK(result.n_patch_faces == 86);
    CHECK(result.islands.empty());
    CHECK(std::fabs(result.area_diff_fraction) < kCleanAreaDiffBound);
}

TEST_CASE("disc magnitude_factor 2.0 has a clean, fully-covering decomposition", "[island_coverage]") {
    auto result = island_check::checkCoverage(fixturePath("disc_mf2.0", "quad.vtk"), patchPaths("disc_mf2.0", 5));

    CHECK(result.n_quad_faces == 136);
    CHECK(result.n_patch_faces == 94);
    CHECK(result.islands.empty());
    CHECK(std::fabs(result.area_diff_fraction) < kCleanAreaDiffBound);
}

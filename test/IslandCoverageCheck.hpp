#pragma once

// Geometric coverage check between a raw quad mesh (quad.vtk, as written by
// Outputpatch/quadgeneration) and the union of per-patch structured grids
// (patchN.vtk, same source) that Optimizer::extract_patch produces.
//
// This is a C++ port of the Python prototype written during the
// magnitude_factor-1.4/1.6 island investigation (see
// CLAUDE.md's "Patch-extraction bug fixes" section). Ported here so it can
// run as a fast Catch2 test against frozen fixtures instead of a one-off
// script a human has to remember to re-run.
//
// Why this check, and not an index-level diff on patch.txt's SUBPATCH
// entries: patch.txt's SUBPATCH entries are indices into O_compact_new, a
// *compacted*, structured per-patch grid of points (see Outputpatch in
// source/parametrizer-mesh.cpp) - a different, smaller index space than
// quad.vtk's raw mesh vertex/face ids. The two numberings do not correspond
// face-for-face or vertex-for-vertex, so a direct set-difference of ids is
// meaningless. patchN.vtk, by contrast, is written in the *same physical
// coordinate space* as quad.vtk (Outputpatch applies the same
// normalize_scale/normalize_offset to both), so geometric (coordinate-based)
// comparison is the correct level to diff at.
//
// Method: parse quad.vtk and every patchN.vtk into (points, quad-faces);
// for each quad.vtk face, compute its centroid and test via point-in-polygon
// whether any patch face covers it. A face covered by zero patch faces is an
// "island" - a region the patch decomposition silently drops. A total-area
// comparison (shoelace formula, quad.vtk vs. the sum of all patch faces) is
// a secondary cross-check for overlap/double-counting that the centroid test
// alone could mask.
//
// On a clean decomposition this reports 0 uncovered faces and an area
// difference in the 0.02-0.05% range - ordinary noise from the patch grid
// being a coarser, differently-triangulated subdivision of the same region,
// not a bug signal. A real island is far larger (the two known cases: 1.4
// pre-fix at 0.871%, 1.6 at 2.327%).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace island_check {

struct Point3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

// A parsed VTK UNSTRUCTURED_GRID: points plus quad-cell vertex-index lists.
// Only cell type 9 (VTK_QUAD) is kept, matching every file this is applied
// to (patchN.vtk/quad.vtk, as written by Outputpatch); any other cell type
// present would silently reduce coverage and get caught by the area
// cross-check rather than crash this parser.
struct ParsedMesh {
    std::vector<Point3> points;
    std::vector<std::array<int, 4>> quads;
};

inline ParsedMesh parseVtkQuads(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("IslandCoverageCheck: cannot open " + path);
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }

    int n_points = 0, n_cells = 0;
    int points_start = -1, cells_start = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].rfind("POINTS", 0) == 0) {
            std::istringstream iss(lines[i]);
            std::string tag;
            iss >> tag >> n_points;
            points_start = static_cast<int>(i) + 1;
        }
        if (lines[i].rfind("CELLS", 0) == 0) {
            std::istringstream iss(lines[i]);
            std::string tag;
            int total_ints;
            iss >> tag >> n_cells >> total_ints;
            cells_start = static_cast<int>(i) + 1;
        }
    }
    if (points_start < 0 || cells_start < 0) {
        throw std::runtime_error("IslandCoverageCheck: " + path + " missing POINTS or CELLS header");
    }

    ParsedMesh mesh;
    mesh.points.reserve(static_cast<size_t>(n_points));
    {
        int count = 0;
        int i = points_start;
        while (count < n_points && i < static_cast<int>(lines.size())) {
            std::istringstream iss(lines[i]);
            double v;
            std::vector<double> vals;
            while (iss >> v) vals.push_back(v);
            for (size_t j = 0; j + 2 < vals.size() + 1 && count < n_points; j += 3) {
                Point3 p{vals[j], vals[j + 1], vals[j + 2]};
                mesh.points.push_back(p);
                count += 1;
            }
            i += 1;
        }
    }

    {
        int i = cells_start;
        for (int c = 0; c < n_cells && i < static_cast<int>(lines.size()); ++c, ++i) {
            std::istringstream iss(lines[i]);
            std::vector<int> vals;
            int v;
            while (iss >> v) vals.push_back(v);
            if (vals.empty()) continue;
            int k = vals[0];
            if (k == 4 && static_cast<int>(vals.size()) >= 5) {
                mesh.quads.push_back({vals[1], vals[2], vals[3], vals[4]});
            }
            // Non-quad cells are silently skipped, same rationale as the
            // Python prototype: the area cross-check below would catch a
            // silently-wrong skip here via a total-area mismatch.
        }
    }

    return mesh;
}

inline std::array<double, 2> centroid2d(const std::array<std::array<double, 2>, 4>& poly) {
    double cx = 0.0, cy = 0.0;
    for (const auto& p : poly) {
        cx += p[0];
        cy += p[1];
    }
    return {cx / 4.0, cy / 4.0};
}

inline double shoelaceArea(const std::array<std::array<double, 2>, 4>& poly) {
    double s = 0.0;
    for (int i = 0; i < 4; ++i) {
        const auto& p1 = poly[i];
        const auto& p2 = poly[(i + 1) % 4];
        s += p1[0] * p2[1] - p2[0] * p1[1];
    }
    return std::fabs(s) / 2.0;
}

// Ray-casting point-in-polygon test, inclusive of the boundary within eps -
// a centroid landing exactly on a shared patch-boundary edge should count
// as covered, not as a false island. Direct port of the Python prototype's
// point_in_polygon.
inline bool pointInPolygon(const std::array<double, 2>& pt, const std::array<std::array<double, 2>, 4>& poly,
                           double eps = 1e-9) {
    double x = pt[0], y = pt[1];
    bool inside = false;
    for (int i = 0; i < 4; ++i) {
        double x1 = poly[i][0], y1 = poly[i][1];
        double x2 = poly[(i + 1) % 4][0], y2 = poly[(i + 1) % 4][1];
        if (std::min(x1, x2) - eps <= x && x <= std::max(x1, x2) + eps && std::min(y1, y2) - eps <= y &&
            y <= std::max(y1, y2) + eps) {
            double cross = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
            double seg_len2 = (x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1);
            if (seg_len2 > 0 && (cross * cross) / seg_len2 < eps) {
                return true;
            }
        }
        if ((y1 > y) != (y2 > y)) {
            double x_int = x1 + (y - y1) / (y2 - y1) * (x2 - x1);
            if (x_int > x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

struct IslandFace {
    int face_index = 0;
    std::array<int, 4> verts{};
    std::array<double, 2> centroid{};
};

struct CoverageResult {
    int n_quad_points = 0;
    int n_quad_faces = 0;
    int n_patch_faces = 0;
    double quad_total_area = 0.0;
    double patch_total_area = 0.0;
    double area_diff_fraction = 0.0;  // (quad_area - patch_area) / quad_area
    std::vector<IslandFace> islands;
};

inline std::array<std::array<double, 2>, 4> facePoly(const ParsedMesh& mesh, const std::array<int, 4>& face) {
    std::array<std::array<double, 2>, 4> poly;
    for (int i = 0; i < 4; ++i) {
        poly[i] = {mesh.points[static_cast<size_t>(face[i])].x, mesh.points[static_cast<size_t>(face[i])].y};
    }
    return poly;
}

// runDir must contain quad.vtk and one or more patchN.vtk files (N = 0, 1,
// 2, ... with no gaps assumed - patchPaths lists them explicitly instead of
// globbing, since a fixed fixture set's file list is known up front and
// explicit beats a directory scan for a frozen test fixture).
inline CoverageResult checkCoverage(const std::string& quadPath, const std::vector<std::string>& patchPaths) {
    ParsedMesh quadMesh = parseVtkQuads(quadPath);

    std::vector<std::array<std::array<double, 2>, 4>> patchFacePolys;
    double patchTotalArea = 0.0;
    for (const auto& pp : patchPaths) {
        ParsedMesh patchMesh = parseVtkQuads(pp);
        for (const auto& face : patchMesh.quads) {
            auto poly = facePoly(patchMesh, face);
            patchFacePolys.push_back(poly);
            patchTotalArea += shoelaceArea(poly);
        }
    }

    CoverageResult result;
    result.n_quad_points = static_cast<int>(quadMesh.points.size());
    result.n_quad_faces = static_cast<int>(quadMesh.quads.size());
    result.n_patch_faces = static_cast<int>(patchFacePolys.size());
    result.patch_total_area = patchTotalArea;

    double quadTotalArea = 0.0;
    for (int fi = 0; fi < static_cast<int>(quadMesh.quads.size()); ++fi) {
        const auto& face = quadMesh.quads[static_cast<size_t>(fi)];
        auto poly = facePoly(quadMesh, face);
        quadTotalArea += shoelaceArea(poly);
        auto c = centroid2d(poly);

        bool covered = false;
        for (const auto& pf : patchFacePolys) {
            if (pointInPolygon(c, pf)) {
                covered = true;
                break;
            }
        }
        if (!covered) {
            IslandFace isl;
            isl.face_index = fi;
            isl.verts = face;
            isl.centroid = c;
            result.islands.push_back(isl);
        }
    }
    result.quad_total_area = quadTotalArea;
    result.area_diff_fraction = (quadTotalArea - patchTotalArea) / quadTotalArea;

    return result;
}

}  // namespace island_check

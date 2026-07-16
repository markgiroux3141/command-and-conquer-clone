// mapconv — convert a map to the engine's custom-level format and verify the
// round-trip. Loads <in.ini> (RA / TD / custom), writes it as a custom level
// (<out.ini> + <out.bin>), reloads that, and diffs the two in-memory maps.
//
//   mapconv <in.ini> <out.ini>
//
// Doubles as the Phase-A self-test: a TD source should round-trip with zero
// cell/object/waypoint mismatches. (RA sources whose template ids exceed 255
// or that use per-cell overlay will report expected losses — the custom .bin
// is a single byte per template and stores overlay TD-style.)

#include "game/map.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int diffCells(const game::MapFile& a, const game::MapFile& b) {
    int mism = 0;
    size_t n = std::min(a.cells.size(), b.cells.size());
    for (size_t i = 0; i < n; i++) {
        if (a.cells[i].templateId != b.cells[i].templateId ||
            a.cells[i].icon != b.cells[i].icon) {
            if (mism < 10)
                std::printf("  cell %zu: (%u,%u) -> (%u,%u)\n", i,
                            a.cells[i].templateId, a.cells[i].icon,
                            b.cells[i].templateId, b.cells[i].icon);
            mism++;
        }
    }
    return mism;
}

int diffObjects(const char* label, const std::vector<game::MapFile::Object>& a,
                const std::vector<game::MapFile::Object>& b) {
    int mism = 0;
    if (a.size() != b.size()) {
        std::printf("  %s: count %zu -> %zu\n", label, a.size(), b.size());
        mism++;
    }
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.house != y.house || x.type != y.type || x.health != y.health ||
            x.cell != y.cell || x.facing != y.facing || x.subcell != y.subcell ||
            x.mission != y.mission) {
            if (mism < 10)
                std::printf("  %s[%zu]: %s/%s@%d f%d -> %s/%s@%d f%d\n", label, i,
                            x.house.c_str(), x.type.c_str(), x.cell, x.facing,
                            y.house.c_str(), y.type.c_str(), y.cell, y.facing);
            mism++;
        }
    }
    return mism;
}

int diffTerrain(const char* label,
                const std::vector<game::MapFile::TerrainObject>& a,
                const std::vector<game::MapFile::TerrainObject>& b) {
    int mism = 0;
    if (a.size() != b.size()) {
        std::printf("  %s: count %zu -> %zu\n", label, a.size(), b.size());
        mism++;
    }
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++)
        if (a[i].cell != b[i].cell || a[i].name != b[i].name)
            mism++;
    return mism;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: mapconv <in.ini> <out.ini>\n");
        return 2;
    }
    std::string inPath = argv[1];
    std::string outPath = argv[2];

    try {
        game::MapFile src = game::MapFile::load(inPath);
        std::printf("loaded %s: theater=%s bounds=%d,%d %dx%d "
                    "(struct=%zu units=%zu inf=%zu terrain=%zu overlay=%zu wp=%zu)\n",
                    inPath.c_str(), src.theater.c_str(), src.x, src.y, src.width,
                    src.height, src.structures.size(), src.units.size(),
                    src.infantry.size(), src.terrain.size(), src.tdOverlay.size(),
                    src.waypoints.size());

        src.save(outPath);
        std::printf("saved custom level: %s (+ .bin)\n", outPath.c_str());

        game::MapFile rt = game::MapFile::load(outPath);

        int mism = 0;
        mism += diffCells(src, rt);
        mism += diffObjects("STRUCTURES", src.structures, rt.structures);
        mism += diffObjects("UNITS", src.units, rt.units);
        mism += diffObjects("INFANTRY", src.infantry, rt.infantry);
        mism += diffTerrain("TERRAIN", src.terrain, rt.terrain);
        mism += diffTerrain("OVERLAY", src.tdOverlay, rt.tdOverlay);

        // Waypoints: compare only set entries present in both.
        size_t wpN = std::min(src.waypoints.size(), rt.waypoints.size());
        for (size_t i = 0; i < wpN; i++)
            if (src.waypoints[i] != rt.waypoints[i])
                mism++;

        if (mism == 0)
            std::printf("ROUND-TRIP OK: no mismatches.\n");
        else
            std::printf("ROUND-TRIP: %d mismatch(es) (see above; some are "
                        "expected for RA sources).\n",
                        mism);
        return mism == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mapconv error: %s\n", e.what());
        return 2;
    }
}

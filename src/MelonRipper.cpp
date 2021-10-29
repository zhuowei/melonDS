#include <stdio.h>
#include <time.h>
#include <string.h>
#include <vector>
#include "types.h"
#include "GPU.h"
#include "MelonRipper.h"

namespace MelonRipper {

// Polys sumitted on frame 1 won't be used until frame 2, so for the polys
// from frame 1 we need the textures/state from frame 2. So we need to double
// buffer.
//
// Flow is
// - User requests dump.
// - At the next FlushRequest, we start recording polys to DumpFile1.
// - At the next FlushRequest, we finish recording polys. DumpFile1 is
//   moved to DumpFile2.
// - At the next RenderFrame, attach VRAM and other global state to
//   DumpFile2 and write out to disk.

// True if we're recording polys to DumpFile1
bool IsDumping = false;

static std::vector<u8> DumpFile1;
static std::vector<u8> DumpFile2;

// Counter for frames dumped so far
static unsigned FrameCntr = 0;

// How many frames we have left to do
static unsigned NumScheduled = 0;

// Total number of frames to dump
static unsigned TotRequestedFrames = 0;

// String with the time the rip started (for filename)
char TimeStr[64];

static void DumpVRAM();
static void DumpDispCnt();
static void DumpToonTable();

static void DumpOp(std::vector<u8>& v, const char* s) {
    v.push_back(s[0]);
    v.push_back(s[1]);
    v.push_back(s[2]);
    v.push_back(s[3]);
}

static void DumpU32(std::vector<u8>& v, u32 x) {
    v.push_back((x >> 0 )&0xffU);
    v.push_back((x >> 8 )&0xffU);
    v.push_back((x >> 16)&0xffU);
    v.push_back((x >> 24)&0xffU);
}

static void DumpU16(std::vector<u8>& v, u16 x) {
    v.push_back((x >> 0)&0xffU);
    v.push_back((x >> 8)&0xffU);
}

static void WriteDumpFile(const char* filename) {
    bool ok = false;
    FILE* fp = fopen(filename, "wb+");
    if (fp) {
        if (fwrite(DumpFile2.data(), DumpFile2.size(), 1, fp) == 1) {
            ok = true;
        }
        fclose(fp);
    }

    if (ok) {
        printf("MelonRipper: ripped frame to %s\n", filename);
    } else {
        printf("MelonRipper: i/o error writing to %s\n", filename);
    }
}

void RequestRip(unsigned nframes) {
    if (TotRequestedFrames) {
        // Already doing a rip, ignore request
        return;
    }
    TotRequestedFrames = nframes;
    NumScheduled = nframes;
}

void FlushRequest() {
    if (!DumpFile2.empty()) {
        // This shouldn't happen
        return;
    }

    DumpFile1.swap(DumpFile2);

    if (NumScheduled == 0) {
        IsDumping = false;
        return;
    }

    IsDumping = true;
    --NumScheduled;

    DumpFile1.reserve(2*1024*1024);
    DumpFile1.clear();
    const char magic[24] = "melon ripper v2";
    DumpFile1.insert(DumpFile1.begin(), &magic[0], &magic[sizeof(magic)]);
}

void RenderFrame() {
    if (DumpFile2.empty()) return;

    // Finalize
    DumpVRAM();
    DumpDispCnt();
    DumpToonTable();

    // Format time on the first frame of the rip
    if (FrameCntr == 0) {
        time_t t;
        struct tm *tmp;
        t = time(nullptr);
        tmp = localtime(&t);
        if (!tmp || !strftime(TimeStr, sizeof(TimeStr), "%Y-%m-%d-%H-%M-%S", tmp)) {
            strcpy(TimeStr, "");  // fallback
        }
    }

    // Write to a file with the time in the name
    char filename[96] = {};
    if (TotRequestedFrames == 1) {
        snprintf(filename, sizeof(filename), "melonrip-%s.dump", TimeStr);
    } else {
        snprintf(filename, sizeof(filename), "melonrip-%s_f%d.dump", TimeStr, FrameCntr+1);
    }

    WriteDumpFile(filename);

    DumpFile2.clear();

    ++FrameCntr;
    if (FrameCntr == TotRequestedFrames) {
        // Done
        FrameCntr = TotRequestedFrames = 0;
    }
}

void Polygon(GPU3D::Vertex verts[4], int nverts) {
    DumpOp(DumpFile1, nverts == 3 ? "TRI " : "QUAD");

    for (int i = 0; i != nverts; ++i) {
        const GPU3D::Vertex& v = verts[i];
        DumpU32(DumpFile1, v.WorldPosition[0]);
        DumpU32(DumpFile1, v.WorldPosition[1]);
        DumpU32(DumpFile1, v.WorldPosition[2]);
        DumpU32(DumpFile1, v.Color[0]);
        DumpU32(DumpFile1, v.Color[1]);
        DumpU32(DumpFile1, v.Color[2]);
        DumpU16(DumpFile1, v.TexCoords[0]);
        DumpU16(DumpFile1, v.TexCoords[1]);
    }
}

void TexParam(u32 param) {
    DumpOp(DumpFile1, "TPRM");
    DumpU32(DumpFile1, param);
}

void TexPalette(u32 pal) {
    DumpOp(DumpFile1, "TPLT");
    DumpU32(DumpFile1, pal);
}

void PolygonAttr(u32 attr) {
    DumpOp(DumpFile1, "PATR");
    DumpU32(DumpFile1, attr);
}

void DumpVRAM() {
    DumpOp(DumpFile2, "VRAM");
    for (int i = 0; i != 4; ++i) DumpU32(DumpFile2, GPU::VRAMMap_Texture[i]);
    for (int i = 0; i != 8; ++i) DumpU32(DumpFile2, GPU::VRAMMap_TexPal[i]);
#define DUMP_BANK(bank) DumpFile2.insert(DumpFile2.end(), &GPU::bank[0], &GPU::bank[sizeof(GPU::bank)])
    DUMP_BANK(VRAM_A);
    DUMP_BANK(VRAM_B);
    DUMP_BANK(VRAM_C);
    DUMP_BANK(VRAM_D);
    DUMP_BANK(VRAM_E);
    DUMP_BANK(VRAM_F);
    DUMP_BANK(VRAM_G);
#undef DUMP_BANK
}

void DumpDispCnt() {
    DumpOp(DumpFile2, "DISP");
    DumpU32(DumpFile2, GPU3D::RenderDispCnt);
}

void DumpToonTable() {
    DumpOp(DumpFile2, "TOON");
    for (int i = 0; i != 32; ++i) {
        DumpU16(DumpFile2, GPU3D::RenderToonTable[i]);
    }
}

}

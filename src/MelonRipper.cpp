#include <stdio.h>
#include <time.h>
#include <string.h>
#include <vector>
#include "types.h"
#include "GPU.h"
#include "MelonRipper.h"

namespace MelonRipper {

bool IsDumping = false;
static bool IsScheduled = false;
static std::vector<u8> DumpFile;

static void DumpOp(const char* s) {
    DumpFile.push_back(s[0]);
    DumpFile.push_back(s[1]);
    DumpFile.push_back(s[2]);
    DumpFile.push_back(s[3]);
}

static void DumpU32(u32 x) {
    DumpFile.push_back((x >> 0 )&0xffU);
    DumpFile.push_back((x >> 8 )&0xffU);
    DumpFile.push_back((x >> 16)&0xffU);
    DumpFile.push_back((x >> 24)&0xffU);
}

static void DumpU16(u16 x) {
    DumpFile.push_back((x >> 0)&0xffU);
    DumpFile.push_back((x >> 8)&0xffU);
}

static void DumpVRAM();

static void WriteDumpFile(const char* filename) {
    bool ok = false;
    FILE* fp = fopen(filename, "wb+");
    if (fp) {
        if (fwrite(DumpFile.data(), DumpFile.size(), 1, fp) == 1) {
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

void ScheduleDumpForNextFrame() {
    IsScheduled = true;
}

void StartFrame() {
    if (IsScheduled) {
        IsScheduled = false;
        IsDumping = true;
    }

    if (!IsDumping) return;

    DumpFile.clear();
    DumpFile.reserve(2*1024*1024);
    const char magic[24] = {
        'm','e','l','o','n',' ','r','i','p','p','e','r',' ','v','1'
    };
    DumpFile.insert(DumpFile.begin(), &magic[0], &magic[sizeof(magic)]);
}

void FinishFrame() {
    if (!IsDumping) return;

    DumpVRAM();

    // Write to a file in the cur dir with the time in the name
    char filename[96] = {};
    time_t t;
    struct tm *tmp;
    t = time(nullptr);
    tmp = localtime(&t);
    if (!tmp || !strftime(filename, sizeof(filename), "melonrip-%Y-%m-%d-%H-%M-%S.dump", tmp)) {
        strcpy(filename, "melonrip.dump"); // fallback
    }

    WriteDumpFile(filename);

    IsDumping = false;
    DumpFile.clear();
}

void Polygon(GPU3D::Vertex verts[4], int nverts) {
    DumpOp(nverts == 3 ? "TRI " : "QUAD");

    for (int i = 0; i != nverts; ++i) {
        const GPU3D::Vertex& v = verts[i];
        DumpU32(v.WorldPosition[0]);
        DumpU32(v.WorldPosition[1]);
        DumpU32(v.WorldPosition[2]);
        DumpU32(v.Color[0]);
        DumpU32(v.Color[1]);
        DumpU32(v.Color[2]);
        DumpU16(v.TexCoords[0]);
        DumpU16(v.TexCoords[1]);
    }
}

void TexParam(u32 param) {
    DumpOp("TPRM");
    DumpU32(param);
}

void TexPalette(u32 pal) {
    DumpOp("TPLT");
    DumpU32(pal);
}

void PolygonAttr(u32 attr) {
    DumpOp("PATR");
    DumpU32(attr);
}

void DumpVRAM() {
    DumpOp("VRAM");
    for (int i = 0; i != 4; ++i) DumpU32(GPU::VRAMMap_Texture[i]);
    for (int i = 0; i != 8; ++i) DumpU32(GPU::VRAMMap_TexPal[i]);
#define DUMP_BANK(bank) DumpFile.insert(DumpFile.end(), &GPU::bank[0], &GPU::bank[sizeof(GPU::bank)])
    DUMP_BANK(VRAM_A);
    DUMP_BANK(VRAM_B);
    DUMP_BANK(VRAM_C);
    DUMP_BANK(VRAM_D);
    DUMP_BANK(VRAM_E);
    DUMP_BANK(VRAM_F);
    DUMP_BANK(VRAM_G);
#undef DUMP_BANK
}

}

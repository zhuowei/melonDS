#include <time.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "types.h"
#include "GPU.h"
#include "MelonRipper.h"
#include "NDSCart.h"

namespace MelonRipper {

bool IsDumping;

// Request for a (sequence of) rips.
struct Request {
    unsigned num_frames_requested;
    unsigned num_frames_done;
    unsigned next_frame_number;
    std::string filename_base;

    void Start(unsigned frames) {
        num_frames_requested = frames;
        num_frames_done = 0;
        next_frame_number = 0;
    }

    void Done() {
        num_frames_requested = 0;
    }

    bool IsDone() const {
        return num_frames_done >= num_frames_requested;
    }
};

// 3D screenshot. Records GPU commands, VRAM data, etc into a buffer.
struct Rip {
    std::vector<u8> data;
    std::string filename;

    void Start() {
        const char magic[24] = "melon ripper v2";
        data.reserve(2*1024*1024);
        data.clear();
        data.insert(data.begin(), &magic[0], &magic[sizeof(magic)]);
    }

    void Done() {
        data.clear();
    }

    operator bool() const {
        return !data.empty();
    }

    void WriteOpcode(const char* s) {
        data.insert(data.end(), s, s+4);
    }

    void WriteU16(u16 x) {
        data.push_back(x & 0xFF);
        data.push_back(x >> 8);
    }

    void WriteU32(u32 x) {
        data.push_back((x >> 0) & 0xFF);
        data.push_back((x >> 8) & 0xFF);
        data.push_back((x >> 16) & 0xFF);
        data.push_back((x >> 24) & 0xFF);
    }

    void WritePolygon(GPU3D::Vertex verts[4], int nverts) {
        WriteOpcode(nverts == 3 ? "TRI " : "QUAD");

        for (int i = 0; i != nverts; ++i) {
            const GPU3D::Vertex& v = verts[i];

            WriteU32(v.WorldPosition[0]);
            WriteU32(v.WorldPosition[1]);
            WriteU32(v.WorldPosition[2]);
            WriteU32(v.Color[0]);
            WriteU32(v.Color[1]);
            WriteU32(v.Color[2]);
            WriteU16(v.TexCoords[0]);
            WriteU16(v.TexCoords[1]);
        }
    }

    void WriteTexParam(u32 param) {
        WriteOpcode("TPRM");
        WriteU32(param);
    }

    void WriteTexPalette(u32 pal) {
        WriteOpcode("TPLT");
        WriteU32(pal);
    }

    void WritePolygonAttr(u32 attr) {
        WriteOpcode("PATR");
        WriteU32(attr);
    }

    void WriteVRAM() {
        WriteOpcode("VRAM");

        for (int i = 0; i != 4; ++i)
            WriteU32(GPU::VRAMMap_Texture[i]);
        for (int i = 0; i != 8; ++i)
            WriteU32(GPU::VRAMMap_TexPal[i]);

        auto dump_bank = [&](const u8* bank, size_t size) {
            data.insert(data.end(), bank, bank + size);
        };
        dump_bank(GPU::VRAM_A, sizeof(GPU::VRAM_A));
        dump_bank(GPU::VRAM_B, sizeof(GPU::VRAM_B));
        dump_bank(GPU::VRAM_C, sizeof(GPU::VRAM_C));
        dump_bank(GPU::VRAM_D, sizeof(GPU::VRAM_D));
        dump_bank(GPU::VRAM_E, sizeof(GPU::VRAM_E));
        dump_bank(GPU::VRAM_F, sizeof(GPU::VRAM_F));
        dump_bank(GPU::VRAM_G, sizeof(GPU::VRAM_G));
    }

    void WriteDispCnt() {
        WriteOpcode("DISP");
        WriteU32(GPU3D::RenderDispCnt);
    }

    void WriteToonTable() {
        WriteOpcode("TOON");
        for (int i = 0; i != 32; ++i) {
            WriteU16(GPU3D::RenderToonTable[i]);
        }
    }
};

static Request CurRequest;

// We need two Rips because of double buffering. The lifecycle goes
//
// 1. Wait for the game to flush the current frame it's working on.
// 2. Begin recording commands into CurRip.
// 3. The next time the game flushes, move CurRip into PendingRip. We
//    begin recording the second rip into CurRip now.
// 4. The next time the GPU renders a frame it, finalize PendingRip by
//    attaching the state of VRAM, etc. and write it out. We need to
//    wait until it renders because VRAM may change between when the
//    frame was flushed and when it gets rendered.
static Rip CurRip, PendingRip;

void Polygon(GPU3D::Vertex verts[4], int nverts)
{
    CurRip.WritePolygon(verts, nverts);
}

void TexParam(u32 param)
{
    CurRip.WriteTexParam(param);
}

void TexPalette(u32 pal) {
    CurRip.WriteTexPalette(pal);
}

void PolygonAttr(u32 attr)
{
    CurRip.WritePolygonAttr(attr);
}

char ConvertToFilenameChar(char c)
{
    if (('0' <= c && c <= '9') || ('a' <= c && c <= 'z'))
        return c;
    if ('A' <= c && c <= 'Z')
        return c - 'A' + 'a';
    return 0;
}

void InitRequestFilename()
{
    std::string& s = CurRequest.filename_base;
    s.clear();

    // <GameTitle>
    for (int i = 0; i != 12; ++i) {
        char c = NDSCart::Header.GameTitle[i];
        c = ConvertToFilenameChar(c);
        if (c) s.push_back(c);
    }

    // Fallback if empty for some reason
    if (s.empty())
        s = "melonrip";

    // -YYYY-MM-DD-HH-MM-SS
    time_t t;
    struct tm *tmp;
    t = time(nullptr);
    tmp = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "-%Y-%m-%d-%H-%M-%S", tmp);
    s += buf;
}

void InitRipFilename()
{
    CurRip.filename = CurRequest.filename_base;

    // Append _f{frame number} if ripping multiple frames
    if (CurRequest.num_frames_requested > 1) {
        CurRip.filename += "_f";
        CurRip.filename += std::to_string(CurRequest.next_frame_number);
    }

    CurRip.filename += ".dump";
}

void BeginRip()
{
    CurRip.Start();
    InitRipFilename();
    CurRequest.next_frame_number++;
}

void MoveCurRipToPending()
{
    if (!CurRip)
        return;

    if (PendingRip)
        return;

    std::swap(PendingRip, CurRip);
}

void WritePendingRip()
{
    const char* filename = PendingRip.filename.c_str();

    bool ok = false;
    FILE* fp = fopen(filename, "wb+");
    if (fp) {
        ok = fwrite(PendingRip.data.data(), PendingRip.data.size(), 1, fp) == 1;
        fclose(fp);
    }

    if (ok)
        printf("MelonRipper: ripped frame to %s\n", filename);
    else
        printf("MelonRipper: error writing %s\n", filename);
}

void FinishPendingRip()
{
    // Write the last of the data
    PendingRip.WriteVRAM();
    PendingRip.WriteDispCnt();
    PendingRip.WriteToonTable();

    WritePendingRip();
    PendingRip.Done();
    CurRequest.num_frames_done++;
}

void RequestRip(unsigned num_frames)
{
    if (!CurRequest.IsDone())
        return;

    CurRequest.Start(num_frames);
    InitRequestFilename();
}

void NotifyFlushRequest()
{
    IsDumping = false;

    if (CurRequest.IsDone())
        return;

    MoveCurRipToPending();

    if (CurRip) {
        // In case PendingRip still exists which blocked the CurRequest
        // from being moved. This can only happen if there are two flush
        // requests with no render frame in between, which shouldn't
        // happen.
        return;
    }

    if (CurRequest.next_frame_number >= CurRequest.num_frames_requested) {
        // The last rip of the request is pending, but not finished yet.
        // We don't need to start a new one. This also shouldn't happen,
        // for the same reason.
        return;
    }

    BeginRip();
    IsDumping = true;
}

void NotifyRenderFrame()
{
    if (PendingRip)
        FinishPendingRip();
}

}

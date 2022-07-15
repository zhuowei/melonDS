#include "GPU3D.h"

namespace MelonRipper {

// Request to rip the next num_frames frames.
void RequestRip(unsigned num_frames = 1);

// Currently dumping GPU commands?
extern bool IsDumping;

void Polygon(GPU3D::Vertex verts[4], int nverts);
void TexParam(u32);
void TexPalette(u32);
void PolygonAttr(u32);

void NotifyFlushRequest();
void NotifyRenderFrame();

}

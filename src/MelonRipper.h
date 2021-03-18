#include "GPU3D.h"

namespace MelonRipper {

extern bool IsDumping;

void ScheduleDumpForNextFrame();
void StartFrame();
void FinishFrame();
void Polygon(GPU3D::Vertex verts[4], int nverts);
void TexParam(u32);
void TexPalette(u32);
void PolygonAttr(u32);

}

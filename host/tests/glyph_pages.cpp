#include <PageGlyphArena.h>
#include <cassert>
#include <array>
using freeink::font::PageGlyphArena;
int main() {
  PageGlyphArena arena;arena.reset(2*arena.blockBytes);
  size_t a,b,c;uint32_t ga,gb,gc;
  arena.protect(1,0,1);assert(arena.allocate(arena.blockBytes,a,ga));
  arena.protect(1,2,2);assert(arena.allocate(arena.blockBytes,b,gb));
  assert(a!=b && arena.valid(a,ga) && arena.valid(b,gb));
  assert(!arena.allocate(1,c,gc)); // Both complete pages remain protected.
  arena.protect(2,3,3);assert(arena.allocate(arena.blockBytes,c,gc));
  assert(c==a && !arena.valid(a,ga) && arena.valid(b,gb));
  // A shared glyph pins its block for both pages. Abandoning the old page
  // cannot invalidate a bitmap used by the newly displayed page.
  arena.protect(3,4,4);arena.touch(c);
  arena.protect(4,5,5);assert(arena.allocate(arena.blockBytes,b,gb));
  assert(arena.valid(c,gc));assert(!arena.allocate(1,a,ga));
  // Empty glyphs need a stable address; oversized glyphs bypass the cache.
  arena.reset(16*1024);assert(arena.allocate(0,a,ga));assert(arena.valid(a,ga));
  assert(!arena.allocate(32*1024,b,gb));
  // With no page pins, wrap by whole block and invalidate stale generations.
  assert(arena.allocate(16*1024,b,gb));assert(!arena.valid(a,ga));
}

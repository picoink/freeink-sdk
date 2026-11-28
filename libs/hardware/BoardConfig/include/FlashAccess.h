#pragma once
namespace freeink::resources {
// Enable after boot peripherals initialize. The Metalio link wraps both IDF
// cache guard endpoints so NVS/radio writes also participate in display gates.
void coordinateFlashAccess();
}

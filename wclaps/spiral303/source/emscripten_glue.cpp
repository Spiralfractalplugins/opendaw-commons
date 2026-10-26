// The WebCLAP rules ask for an empty memory-growth hook: the module may grow
// its memory, and the host gets notified through this function.
#include <cstddef>

extern "C" void emscripten_notify_memory_growth(size_t index) {(void) index;}

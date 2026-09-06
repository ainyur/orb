// Unity build root. Every module is included here exactly once.
#include "orb.h"
#include "cast/aseprite.c"
#include "cast/inflate.c"
#include "cast/json.c"
#include "cast/orbfile.c"
#include "cast/pack.c"
#include "core/api.c"
#include "core/arena.c"
#include "core/input.c"
#include "core/log.c"
#if defined(ORB_OS_X11)
#include "os/x11.c"
#elif defined(ORB_OS_GDI)
#include "os/gdi.c"
#elif defined(ORB_OS_HEADLESS)
#include "os/headless.c"
#else
#error "define ORB_OS_X11, ORB_OS_GDI, or ORB_OS_HEADLESS"
#endif
#include "cast/cast.c"
#include "core/debug.c"
#include "core/run.c"
#include "graphics/framebuffer.c"
#include "graphics/palette.c"
#include "graphics/sprite.c"

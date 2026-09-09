#include "orb.h"
#include "cast/file.c"

#ifndef ORB_RELEASE
#include "cast/aseprite.c"
#include "cast/cast.c"
#include "cast/inflate.c"
#include "cast/json.c"
#include "cast/pack.c"
#include "cast/wav.c"
#include "core/debug.c"
#endif

#include "audio/mixer.c"
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

#include "core/run.c"
#include "graphics/framebuffer.c"
#include "graphics/palette.c"
#include "graphics/sprite.c"

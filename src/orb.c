#include "orb.h"

// Boot order, not alphabetical: each module before the ones that call it.
// clang-format off
#include "core/arena.c"
#include "core/log.c"
#include "core/asset.c"
#include "cast/file.c"
#include "core/input.c"
#include "graphics/fb.c"
#include "graphics/pal.c"
#include "graphics/sprite.c"
#include "graphics/text.c"
#include "graphics/tilemap.c"
#include "graphics/camera.c"
#include "audio/mixer.c"
#include "core/api.c"

#if defined(ORB_OS_X11)
#include "os/x11.c"
#elif defined(ORB_OS_GDI)
#include "os/gdi.c"
#elif defined(ORB_OS_HEADLESS)
#include "os/headless.c"
#else
#error "define ORB_OS_X11, ORB_OS_GDI, or ORB_OS_HEADLESS"
#endif

#include "core/host.c"

#ifndef ORB_RELEASE
#include "cast/json.c"
#include "cast/inflate.c"
#include "cast/aseprite.c"
#include "cast/pack.c"
#include "cast/wav.c"
#include "cast/ldtk.c"
#include "cast/cast.c"
#include "core/debug.c"
#endif
// clang-format on

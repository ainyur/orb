#include "camera.h"

// One axis: move toward the centered target by (1 - lerp), then keep the
// framebuffer inside the bounds, or center on them when they are smaller.
static float camera_axis(
    float at,
    float target,
    float lerp,
    int bound_at,
    int bound_size,
    int screen
) {
    float centered = target - screen / 2.0f;

    at += (centered - at) * (1 - lerp);

    if (bound_size == 0) return at;
    if (bound_size < screen) return bound_at + bound_size / 2.0f - screen / 2.0f;
    if (at < bound_at) return (float)bound_at;
    if (at > bound_at + bound_size - screen) return (float)(bound_at + bound_size - screen);

    return at;
}

orb_camera orb_camera_update(orb_camera c, orb_size fb) {
    c.at.x = camera_axis(c.at.x, c.target.x, c.lerp, c.bounds.at.x, c.bounds.size.width, fb.width);
    c.at.y =
        camera_axis(c.at.y, c.target.y, c.lerp, c.bounds.at.y, c.bounds.size.height, fb.height);

    return c;
}

#pragma once

// Each argument is evaluated once; typeof keeps the operands' own type.
#define orb_max(x, y)                                                                              \
    ({                                                                                             \
        typeof(x) orb_max_x_ = (x);                                                                \
        typeof(y) orb_max_y_ = (y);                                                                \
        orb_max_x_ > orb_max_y_ ? orb_max_x_ : orb_max_y_;                                         \
    })
#define orb_min(x, y)                                                                              \
    ({                                                                                             \
        typeof(x) orb_min_x_ = (x);                                                                \
        typeof(y) orb_min_y_ = (y);                                                                \
        orb_min_x_ < orb_min_y_ ? orb_min_x_ : orb_min_y_;                                         \
    })

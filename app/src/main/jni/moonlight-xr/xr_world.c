#include "xr_internal.h"

/* Stereo clear until the virtual screens land in a later change. */

void xr_world_init(void) {
}

void xr_world_destroy(void) {
}

void xr_world_update(XrTime display_time, XrDuration period) {
    (void)display_time;
    (void)period;
}

void xr_world_render(const XrView* view, int eye, uint32_t width, uint32_t height) {
    (void)view;
    (void)eye;
    (void)width;
    (void)height;
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.04f, 0.045f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

XrEnvironmentBlendMode xr_world_blend_mode(void) {
    return g_xr.blend_mode;
}

XrCompositionLayerFlags xr_world_layer_flags(void) {
    return 0;
}

int xr_world_push_underlays(const XrCompositionLayerBaseHeader** layers, int count) {
    (void)layers;
    return count;
}

#include "xr_internal.h"

void xr_world_init(void) {
    xr_passthrough_init();
    xr_render_init();
    xr_input_init();
    xr_screens_init();
}

void xr_world_destroy(void) {
    xr_screens_destroy();
    xr_input_destroy();
    xr_render_destroy();
    xr_passthrough_destroy();
}

void xr_world_update(XrTime display_time, XrDuration period) {
    xr_video_begin_frame();
    xr_input_sync(display_time);
    xr_screens_update(display_time, period);
}

void xr_world_render(const XrView* view, int eye, uint32_t width, uint32_t height) {
    xr_render_eye(view, eye, width, height);
}

XrEnvironmentBlendMode xr_world_blend_mode(void) {
    return xr_passthrough_blend_mode();
}

XrCompositionLayerFlags xr_world_layer_flags(void) {
    return xr_passthrough_layer_flags();
}

int xr_world_push_underlays(const XrCompositionLayerBaseHeader** layers, int count) {
    return xr_passthrough_push_underlay(layers, count);
}

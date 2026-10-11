#ifndef MOONLIGHT_XR_GEOM_H
#define MOONLIGHT_XR_GEOM_H

#include <openxr/openxr.h>

/* Yaw of an OpenXR view: 0 looks down -Z, positive yaw looks toward +X. */
float xr_geom_yaw(const XrQuaternionf* orientation);

/* Level screen in front of the head. Local +Z points back along the view. */
void xr_geom_face(XrPosef* pose, const XrPosef* head, float distance, float right_offset);

/* Slide along the screen's local +Z, which points toward the user. */
void xr_geom_dolly(XrPosef* pose, float meters);

void xr_geom_capture(XrPosef* offset, const XrPosef* grip, const XrPosef* screen);
void xr_geom_apply(XrPosef* screen, const XrPosef* grip, const XrPosef* offset);

/* Ray against the same flat quad or cylinder that xr_panel_mesh draws.
 * Cylinder: x = sin(a) * r, z = r * (1 - cos(a)), a in [-half, half]. */
int xr_geom_ray(const XrPosef* pose, float width, float height, float curve,
                const XrVector3f* origin, const XrVector3f* direction,
                float* out_t, float* out_u, float* out_v, XrVector3f* out_point);

#endif

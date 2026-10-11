#include "xr_geom.h"

#include "xr_linear.h"

#include <math.h>

float xr_geom_yaw(const XrQuaternionf* orientation) {
    XrVector3f forward = {0.0f, 0.0f, -1.0f};
    XrVector3f world;
    XrQuaternionf_RotateVector3f(&world, orientation, &forward);
    return atan2f(world.x, -world.z);
}

void xr_geom_face(XrPosef* pose, const XrPosef* head, float distance, float right_offset) {
    float yaw = xr_geom_yaw(&head->orientation);
    float forward_x = sinf(yaw);
    float forward_z = -cosf(yaw);
    float right_x = cosf(yaw);
    float right_z = sinf(yaw);
    pose->position.x = head->position.x + forward_x * distance + right_x * right_offset;
    pose->position.y = head->position.y;
    pose->position.z = head->position.z + forward_z * distance + right_z * right_offset;
    XrVector3f axis = {0.0f, 1.0f, 0.0f};
    XrQuaternionf_CreateFromAxisAngle(&pose->orientation, &axis, -yaw);
}

void xr_geom_dolly(XrPosef* pose, float meters) {
    XrVector3f local = {0.0f, 0.0f, meters};
    XrVector3f world;
    XrQuaternionf_RotateVector3f(&world, &pose->orientation, &local);
    pose->position.x += world.x;
    pose->position.y += world.y;
    pose->position.z += world.z;
}

void xr_geom_capture(XrPosef* offset, const XrPosef* grip, const XrPosef* screen) {
    XrPosef inverse;
    XrPosef_Invert(&inverse, grip);
    XrPosef_Multiply(offset, &inverse, screen);
}

void xr_geom_apply(XrPosef* screen, const XrPosef* grip, const XrPosef* offset) {
    XrPosef_Multiply(screen, grip, offset);
}

static int xr_geom_accept_flat(float width, float height, float x, float y, float* out_u, float* out_v) {
    if (fabsf(x) > width * 0.5f || fabsf(y) > height * 0.5f) {
        return 0;
    }
    *out_u = x / width + 0.5f;
    *out_v = y / height + 0.5f;
    return 1;
}

int xr_geom_ray(const XrPosef* pose, float width, float height, float curve,
                const XrVector3f* origin, const XrVector3f* direction,
                float* out_t, float* out_u, float* out_v, XrVector3f* out_point) {
    if (width < 1.0e-4f || height < 1.0e-4f) {
        return 0;
    }

    XrPosef inverse;
    XrPosef_Invert(&inverse, pose);
    XrVector3f local_origin;
    XrVector3f local_dir;
    XrPosef_TransformVector3f(&local_origin, &inverse, origin);
    XrQuaternionf_RotateVector3f(&local_dir, &inverse.orientation, direction);

    float hit_t = 0.0f;
    float hit_x = 0.0f;
    float hit_y = 0.0f;
    float hit_z = 0.0f;
    float hit_u = 0.0f;
    float hit_v = 0.0f;

    if (curve <= 0.01f) {
        if (fabsf(local_dir.z) < 1.0e-5f) {
            return 0;
        }
        hit_t = -local_origin.z / local_dir.z;
        if (hit_t <= 0.01f) {
            return 0;
        }
        hit_x = local_origin.x + local_dir.x * hit_t;
        hit_y = local_origin.y + local_dir.y * hit_t;
        hit_z = 0.0f;
        if (!xr_geom_accept_flat(width, height, hit_x, hit_y, &hit_u, &hit_v)) {
            return 0;
        }
    } else {
        float radius = curve;
        float dx = local_dir.x;
        float dz = local_dir.z;
        float ox = local_origin.x;
        float oz = local_origin.z;
        float quadratic_a = dx * dx + dz * dz;
        float ozr = oz - radius;
        float quadratic_b = 2.0f * (ox * dx + ozr * dz);
        float quadratic_c = ox * ox + ozr * ozr - radius * radius;
        if (quadratic_a < 1.0e-8f) {
            return 0;
        }
        float disc = quadratic_b * quadratic_b - 4.0f * quadratic_a * quadratic_c;
        if (disc < 0.0f) {
            return 0;
        }
        float root = sqrtf(disc);
        float inverse_denom = 1.0f / (2.0f * quadratic_a);
        float candidates[2] = {(-quadratic_b - root) * inverse_denom, (-quadratic_b + root) * inverse_denom};
        float half = (width * 0.5f) / radius;
        if (half > 1.4f) {
            half = 1.4f;
        }
        if (half < 1.0e-4f) {
            return 0;
        }
        int found = 0;
        float best = 1.0e30f;
        for (int i = 0; i < 2; i++) {
            float t = candidates[i];
            if (t <= 0.01f || t >= best) {
                continue;
            }
            float x = ox + dx * t;
            float y = local_origin.y + local_dir.y * t;
            float z = oz + dz * t;
            if (fabsf(y) > height * 0.5f) {
                continue;
            }
            float angle = atan2f(x, radius - z);
            if (fabsf(angle) > half) {
                continue;
            }
            best = t;
            hit_t = t;
            hit_x = x;
            hit_y = y;
            hit_z = z;
            hit_u = angle / (2.0f * half) + 0.5f;
            hit_v = y / height + 0.5f;
            found = 1;
        }
        if (!found) {
            return 0;
        }
    }

    XrVector3f local = {hit_x, hit_y, hit_z};
    XrPosef_TransformVector3f(out_point, pose, &local);
    *out_t = hit_t;
    *out_u = hit_u;
    *out_v = hit_v;
    return 1;
}

#include "xr_internal.h"

#include "xr_linear.h"

#include <android/log.h>
#include <math.h>
#include <string.h>

typedef struct XrVert {
    float x, y, z, u, v;
} XrVert;

static GLuint xr_compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        log[0] = '\0';
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint xr_program(const char* vert, const char* frag) {
    GLuint vs = xr_compile(GL_VERTEX_SHADER, vert);
    GLuint fs = xr_compile(GL_FRAGMENT_SHADER, frag);
    if (vs == 0 || fs == 0) {
        if (vs != 0) {
            glDeleteShader(vs);
        }
        if (fs != 0) {
            glDeleteShader(fs);
        }
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aUv");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        log[0] = '\0';
        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        __android_log_print(ANDROID_LOG_ERROR, "moonlight-xr", "link: %s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static const char* k_solid_vert =
        "#version 300 es\n"
        "layout(location=0) in vec3 aPos;\n"
        "uniform mat4 uMvp;\n"
        "void main(){ gl_Position = uMvp * vec4(aPos, 1.0); }\n";

static const char* k_solid_frag =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec4 uColor;\n"
        "out vec4 frag;\n"
        "void main(){ frag = uColor; }\n";

static const char* k_tex_vert =
        "#version 300 es\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec2 aUv;\n"
        "uniform mat4 uMvp;\n"
        "out vec2 vUv;\n"
        "void main(){ gl_Position = uMvp * vec4(aPos, 1.0); vUv = aUv; }\n";

static const char* k_tex_frag =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vUv;\n"
        "uniform sampler2D uTex;\n"
        "out vec4 frag;\n"
        "void main(){ frag = texture(uTex, vec2(vUv.x, 1.0 - vUv.y)); }\n";

/* vUv matches the mesh (v = 0 at the bottom). SurfaceTexture's matrix puts the
 * picture upright, so this path does not flip v again. Alpha is forced on so
 * passthrough does not show through the desktop. */
static const char* k_video_frag =
        "#version 300 es\n"
        "#extension GL_OES_EGL_image_external_essl3 : require\n"
        "precision mediump float;\n"
        "in vec2 vUv;\n"
        "uniform samplerExternalOES uTex;\n"
        "uniform mat4 uTexMat;\n"
        "out vec4 frag;\n"
        "void main(){\n"
        "  vec4 color = texture(uTex, (uTexMat * vec4(vUv, 0.0, 1.0)).xy);\n"
        "  frag = vec4(color.rgb, 1.0);\n"
        "}\n";

static void xr_matrix_identity(float m[16]) {
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void xr_configure_video_texture(void) {
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, g_xr.video_tex);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
}

static void xr_draw(GLuint program, const XrVert* verts, int count, GLenum mode, int textured) {
    if (program == 0 || count <= 0) {
        return;
    }
    glUseProgram(program);
    glBindVertexArray(g_xr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_xr.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(XrVert) * (size_t)count), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(XrVert), (void*)0);
    if (textured) {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(XrVert), (void*)(3 * sizeof(float)));
    } else {
        glDisableVertexAttribArray(1);
    }
    glDrawArrays(mode, 0, count);
}

static int xr_panel_mesh(XrVert* verts, int cap, float width, float height, float curve) {
    if (curve <= 0.01f) {
        if (cap < 6) {
            return 0;
        }
        float w = width * 0.5f;
        float h = height * 0.5f;
        const XrVert quad[6] = {
                {-w, -h, 0.0f, 0.0f, 0.0f},
                {w, -h, 0.0f, 1.0f, 0.0f},
                {w, h, 0.0f, 1.0f, 1.0f},
                {-w, -h, 0.0f, 0.0f, 0.0f},
                {w, h, 0.0f, 1.0f, 1.0f},
                {-w, h, 0.0f, 0.0f, 1.0f},
        };
        memcpy(verts, quad, sizeof(quad));
        return 6;
    }

    const int segs = 32;
    if (cap < segs * 6) {
        return 0;
    }
    float radius = curve;
    float half = (width * 0.5f) / radius;
    if (half > 1.4f) {
        half = 1.4f;
    }
    float y0 = -height * 0.5f;
    float y1 = height * 0.5f;
    int count = 0;
    for (int i = 0; i < segs; i++) {
        float a0 = -half + (2.0f * half) * ((float)i / (float)segs);
        float a1 = -half + (2.0f * half) * ((float)(i + 1) / (float)segs);
        float x0 = sinf(a0) * radius;
        float z0 = radius * (1.0f - cosf(a0));
        float x1 = sinf(a1) * radius;
        float z1 = radius * (1.0f - cosf(a1));
        float u0 = (float)i / (float)segs;
        float u1 = (float)(i + 1) / (float)segs;
        XrVert tri[6] = {
                {x0, y0, z0, u0, 0.0f},
                {x1, y0, z1, u1, 0.0f},
                {x1, y1, z1, u1, 1.0f},
                {x0, y0, z0, u0, 0.0f},
                {x1, y1, z1, u1, 1.0f},
                {x0, y1, z0, u0, 1.0f},
        };
        memcpy(verts + count, tri, sizeof(tri));
        count += 6;
    }
    return count;
}

static void xr_mvp(XrMatrix4x4f* mvp, const XrMatrix4x4f* vp, const XrPosef* pose, float z_bias) {
    XrPosef shifted = *pose;
    if (z_bias != 0.0f) {
        XrVector3f local = {0.0f, 0.0f, z_bias};
        XrVector3f world;
        XrQuaternionf_RotateVector3f(&world, &pose->orientation, &local);
        shifted.position.x += world.x;
        shifted.position.y += world.y;
        shifted.position.z += world.z;
    }
    XrMatrix4x4f model;
    XrMatrix4x4f_CreateFromRigidTransform(&model, &shifted);
    XrMatrix4x4f_Multiply(mvp, vp, &model);
}

static void xr_make_placeholder(void) {
    unsigned char pixels[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int border = x < 3 || y < 3 || x >= 61 || y >= 61;
            unsigned char* p = pixels + (y * 64 + x) * 4;
            if (border) {
                p[0] = 70;
                p[1] = 170;
                p[2] = 190;
            } else {
                p[0] = (unsigned char)(12 + y / 8);
                p[1] = (unsigned char)(16 + x / 10);
                p[2] = 28;
            }
            p[3] = 255;
        }
    }
    glGenTextures(1, &g_xr.placeholder_tex);
    glBindTexture(GL_TEXTURE_2D, g_xr.placeholder_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void xr_render_init(void) {
    g_xr.prog_solid = xr_program(k_solid_vert, k_solid_frag);
    g_xr.prog_tex = xr_program(k_tex_vert, k_tex_frag);
    g_xr.prog_video = xr_program(k_tex_vert, k_video_frag);
    if (g_xr.prog_solid != 0) {
        g_xr.loc_solid_mvp = glGetUniformLocation(g_xr.prog_solid, "uMvp");
        g_xr.loc_solid_color = glGetUniformLocation(g_xr.prog_solid, "uColor");
    }
    if (g_xr.prog_tex != 0) {
        g_xr.loc_tex_mvp = glGetUniformLocation(g_xr.prog_tex, "uMvp");
        glUseProgram(g_xr.prog_tex);
        glUniform1i(glGetUniformLocation(g_xr.prog_tex, "uTex"), 0);
    }
    if (g_xr.prog_video != 0) {
        g_xr.loc_video_mvp = glGetUniformLocation(g_xr.prog_video, "uMvp");
        g_xr.loc_video_mat = glGetUniformLocation(g_xr.prog_video, "uTexMat");
        glUseProgram(g_xr.prog_video);
        glUniform1i(glGetUniformLocation(g_xr.prog_video, "uTex"), 0);
    }
    glGenVertexArrays(1, &g_xr.vao);
    glGenBuffers(1, &g_xr.vbo);
    xr_make_placeholder();
    xr_matrix_identity(g_xr.video_matrix);
    g_xr.video_ready = 0;
    glGenTextures(1, &g_xr.video_tex);
    if (g_xr.video_tex != 0) {
        xr_configure_video_texture();
        xr_jni_bind_video_texture((int)g_xr.video_tex);
        xr_configure_video_texture();
    }
}

void xr_video_begin_frame(void) {
    if (g_xr.video_tex == 0) {
        return;
    }
    int epoch = __atomic_load_n(&g_xr.video_epoch, __ATOMIC_ACQUIRE);
    int state = xr_jni_pull_video_frame(g_xr.video_matrix);
    if (state > 0) {
        int epoch_after = __atomic_load_n(&g_xr.video_epoch, __ATOMIC_ACQUIRE);
        if (epoch == epoch_after) {
            __atomic_store_n(&g_xr.video_epoch_seen, epoch, __ATOMIC_RELEASE);
            g_xr.video_ready = 1;
        }
    } else if (state < 0) {
        g_xr.video_ready = 0;
    }
}

void xr_render_destroy(void) {
    xr_jni_release_video();
    g_xr.video_ready = 0;
    if (g_xr.prog_solid != 0) {
        glDeleteProgram(g_xr.prog_solid);
        g_xr.prog_solid = 0;
    }
    if (g_xr.prog_tex != 0) {
        glDeleteProgram(g_xr.prog_tex);
        g_xr.prog_tex = 0;
    }
    if (g_xr.prog_video != 0) {
        glDeleteProgram(g_xr.prog_video);
        g_xr.prog_video = 0;
    }
    if (g_xr.video_tex != 0) {
        glDeleteTextures(1, &g_xr.video_tex);
        g_xr.video_tex = 0;
    }
    if (g_xr.vbo != 0) {
        glDeleteBuffers(1, &g_xr.vbo);
        g_xr.vbo = 0;
    }
    if (g_xr.vao != 0) {
        glDeleteVertexArrays(1, &g_xr.vao);
        g_xr.vao = 0;
    }
    if (g_xr.placeholder_tex != 0) {
        glDeleteTextures(1, &g_xr.placeholder_tex);
        g_xr.placeholder_tex = 0;
    }
}

static void xr_draw_grid(const XrMatrix4x4f* vp) {
    XrVert verts[40];
    int count = 0;
    float y = g_xr.floor_y;
    for (int i = -3; i <= 3; i++) {
        float at = (float)i;
        verts[count++] = (XrVert){-3.0f, y, at, 0.0f, 0.0f};
        verts[count++] = (XrVert){3.0f, y, at, 0.0f, 0.0f};
        verts[count++] = (XrVert){at, y, -3.0f, 0.0f, 0.0f};
        verts[count++] = (XrVert){at, y, 3.0f, 0.0f, 0.0f};
    }
    glUseProgram(g_xr.prog_solid);
    glUniformMatrix4fv(g_xr.loc_solid_mvp, 1, GL_FALSE, vp->m);
    glUniform4f(g_xr.loc_solid_color, 0.18f, 0.20f, 0.24f, 1.0f);
    xr_draw(g_xr.prog_solid, verts, count, GL_LINES, 0);
}

static void xr_draw_lasers(const XrMatrix4x4f* vp) {
    for (int hand = 0; hand < 2; hand++) {
        if (!g_xr.hand_active[hand]) {
            continue;
        }
        XrVector3f origin = g_xr.aim_pose[hand].position;
        XrVector3f forward = {0.0f, 0.0f, -1.0f};
        XrVector3f dir;
        XrQuaternionf_RotateVector3f(&dir, &g_xr.aim_pose[hand].orientation, &forward);
        XrVector3f end;
        if (g_xr.hand_hit[hand]) {
            end = g_xr.hand_point[hand];
        } else {
            end.x = origin.x + dir.x * 3.0f;
            end.y = origin.y + dir.y * 3.0f;
            end.z = origin.z + dir.z * 3.0f;
        }
        XrVert line[2] = {
                {origin.x, origin.y, origin.z, 0.0f, 0.0f},
                {end.x, end.y, end.z, 0.0f, 0.0f},
        };
        glUseProgram(g_xr.prog_solid);
        glUniformMatrix4fv(g_xr.loc_solid_mvp, 1, GL_FALSE, vp->m);
        if (g_xr.hand_hit[hand]) {
            glUniform4f(g_xr.loc_solid_color, 0.35f, 0.95f, 0.75f, 1.0f);
        } else {
            glUniform4f(g_xr.loc_solid_color, 0.85f, 0.30f, 0.25f, 1.0f);
        }
        xr_draw(g_xr.prog_solid, line, 2, GL_LINES, 0);
    }
}

void xr_render_eye(const XrView* view, int eye, uint32_t width, uint32_t height) {
    (void)eye;
    (void)width;
    (void)height;
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float alpha = g_xr.passthrough_on ? 0.0f : 1.0f;
    glClearColor(0.03f, 0.035f, 0.05f, alpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    XrMatrix4x4f camera;
    XrMatrix4x4f view_m;
    XrMatrix4x4f proj;
    XrMatrix4x4f vp;
    XrMatrix4x4f_CreateFromRigidTransform(&camera, &view->pose);
    XrMatrix4x4f_InvertRigidBody(&view_m, &camera);
    XrMatrix4x4f_CreateProjectionFov(&proj, GRAPHICS_OPENGL_ES, view->fov, 0.05f, 100.0f);
    XrMatrix4x4f_Multiply(&vp, &proj, &view_m);

    if (g_xr.have_floor && !g_xr.passthrough_on) {
        xr_draw_grid(&vp);
    }

    XrVert mesh[32 * 6];
    int video_epoch = __atomic_load_n(&g_xr.video_epoch, __ATOMIC_ACQUIRE);
    int video_seen = __atomic_load_n(&g_xr.video_epoch_seen, __ATOMIC_ACQUIRE);
    for (int index = 0; index < XR_MAX_SCREENS; index++) {
        XrScreen* screen = &g_xr.screens[index];
        if (!screen->alive || !screen->visible) {
            continue;
        }
        int grabbed = screen->grab_mask != 0;
        int hovered = (g_xr.hand_hit[0] && g_xr.hand_hover[0] == index) ||
                      (g_xr.hand_hit[1] && g_xr.hand_hover[1] == index);
        int bezel_count = xr_panel_mesh(mesh, 32 * 6, screen->width * 1.035f, screen->height * 1.035f, screen->curve);
        XrMatrix4x4f mvp;
        xr_mvp(&mvp, &vp, &screen->pose, -0.01f);
        glUseProgram(g_xr.prog_solid);
        glUniformMatrix4fv(g_xr.loc_solid_mvp, 1, GL_FALSE, mvp.m);
        if (grabbed) {
            glUniform4f(g_xr.loc_solid_color, 0.95f, 0.55f, 0.15f, 1.0f);
        } else if (hovered) {
            glUniform4f(g_xr.loc_solid_color, 0.25f, 0.78f, 0.95f, 1.0f);
        } else {
            glUniform4f(g_xr.loc_solid_color, 0.10f, 0.11f, 0.14f, 1.0f);
        }
        xr_draw(g_xr.prog_solid, mesh, bezel_count, GL_TRIANGLES, 0);

        int count = xr_panel_mesh(mesh, 32 * 6, screen->width, screen->height, screen->curve);
        xr_mvp(&mvp, &vp, &screen->pose, 0.0f);
        int show_video = screen->kind != XR_SCREEN_MENU && g_xr.video_ready && video_epoch == video_seen &&
                         g_xr.video_tex != 0 && g_xr.prog_video != 0;
        if (show_video) {
            glUseProgram(g_xr.prog_video);
            glUniformMatrix4fv(g_xr.loc_video_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv(g_xr.loc_video_mat, 1, GL_FALSE, g_xr.video_matrix);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, g_xr.video_tex);
            xr_draw(g_xr.prog_video, mesh, count, GL_TRIANGLES, 1);
            continue;
        }
        GLuint texture = screen->kind == XR_SCREEN_MENU ? g_xr.menu_tex : g_xr.placeholder_tex;
        if (texture == 0 || g_xr.prog_tex == 0) {
            continue;
        }
        glUseProgram(g_xr.prog_tex);
        glUniformMatrix4fv(g_xr.loc_tex_mvp, 1, GL_FALSE, mvp.m);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        xr_draw(g_xr.prog_tex, mesh, count, GL_TRIANGLES, 1);
    }

    glDisable(GL_DEPTH_TEST);
    xr_draw_lasers(&vp);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

// libgoddard API + an F3D (Fast3D, "f3d_old" as used by the US release) display-list interpreter.
// The interpreter follows the PC port's RSP emulation (sm64-port gfx_pc.c): matrix stack, RSP lighting and
// texgen are evaluated on the CPU exactly as the microcode would; the RDP state is passed on per batch.
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ultra64.h>

#include "goddard/renderer.h"
#include "goddard/gd_main.h"
#include "gd_api.h"
#undef printf

#define MAX_VERTS (64 * 1024)
#define MAX_BATCHES 1024
#define MAX_TEXTURES 256
#define MTX_STACK 11

static GdVertex sVerts[MAX_VERTS];
static int32_t sVertCount;
static GdBatch sBatches[MAX_BATCHES];
static int32_t sBatchCount;

typedef struct {
    uintptr_t addr;
    int32_t fmt, siz, width, height;
} GdTexture;
static GdTexture sTextures[MAX_TEXTURES];
static int32_t sTextureCount;

typedef struct {
    float obj[3];  // object space, before the modelview (skin vertices are deformed here)
    float eye[3];
    float clip[4];
    float u, v;  // texels
    uint8_t color[4];
} RspVtx;

static struct {
    float mv[MTX_STACK][4][4];
    int32_t mvTop;
    // Goddard loads a perspective into the projection and then multiplies its camera (lookat) into it,
    // so the projection stack is split back into the camera ("view") and the pure perspective
    float view[4][4];
    float persp[4][4];
    float headPersp[4][4];  // last perspective projection (the head view), as opposed to 2D/ortho views
    RspVtx vtx[16];

    Light_t lights[8];
    int32_t numLights;  // directional + ambient (ambient is the last one)
    bool lightsChanged;
    float lightCoeffs[8][3];
    Light_t lookat[2];
    float lookatCoeffs[2][3];

    uint32_t geometryMode;
    uint32_t otherModeL, otherModeH;
    uint32_t combineW0, combineW1;
    uint8_t prim[4], env[4];
    uint16_t texScaleS, texScaleT;
    bool texOn;

    uintptr_t timgAddr;
    uintptr_t loadedAddr;
    struct {
        int32_t fmt, siz, cms, cmt;
        uint16_t uls, ult, lrs, lrt;
    } tile[8];
} rsp;

static float sCursorX, sCursorY;

// ---------------------------------------------------------------------------------------- eyes follow skin
// Goddard's irises are rigid shapes on the eye joints, so stretching the cap or cheeks drags the skin around the
// eyes while the irises stay put. Each iris is moved by the average displacement of the skin around it.
// Shapes are recognised by their display lists' triangle counts (face 877, eyes 82 each).
#define FACE_TRIS 877
#define EYE_TRIS 82
// The eye whites and lids around each iris (Goddard units). Gaze only turns the iris shapes, not the eye joints
// the whites are skinned to, so this skin moves only when the face is stretched.
#define EYE_RING_INNER 0.0f
#define EYE_RING_OUTER 150.0f
#define EYE_RING_MAX 256

typedef struct { int32_t first, count, tris; float m[3][3]; } ShapeRange;
static ShapeRange sShapes[32];
static int32_t sShapeCount;
static float sObjOut[MAX_VERTS][3];  // object-space position of each output vertex
static float sComposed[3][3];        // (modelview * view) rotation of the last vertex load

static float *sFaceRest;              // face object positions at rest, per output vertex
static int32_t sFaceRestCount;
static int32_t sEyeRing[2][EYE_RING_MAX];
static int32_t sEyeRingCount[2];

static void eyes_follow_skin(void) {
    ShapeRange *face = NULL, *eyes[2] = { NULL, NULL };
    for (int32_t i = 0; i < sShapeCount; i++) {
        if (sShapes[i].tris == FACE_TRIS && face == NULL) face = &sShapes[i];
        else if (sShapes[i].tris == EYE_TRIS) {
            if (eyes[0] == NULL) eyes[0] = &sShapes[i];
            else if (eyes[1] == NULL) eyes[1] = &sShapes[i];
        }
    }
    // Only whole frames: during the intro the head starts beyond the far plane and is partly culled
    if (face == NULL || eyes[0] == NULL || eyes[1] == NULL) return;
    if (face->count != FACE_TRIS * 3 || eyes[0]->count != EYE_TRIS * 3 || eyes[1]->count != EYE_TRIS * 3) return;

    if (sFaceRest == NULL) {
        // First frame: the skin is undeformed. Remember it, and the ring of skin around each iris.
        sFaceRestCount = face->count;
        sFaceRest = malloc(sizeof(float) * 3 * face->count);
        memcpy(sFaceRest, sObjOut[face->first], sizeof(float) * 3 * face->count);
        for (int32_t e = 0; e < 2; e++) {
            float c[3] = { 0, 0, 0 };
            for (int32_t i = 0; i < eyes[e]->count; i++)
                for (int32_t k = 0; k < 3; k++) c[k] += sVerts[eyes[e]->first + i].pos[k] / eyes[e]->count;
            sEyeRingCount[e] = 0;
            for (int32_t i = 0; i < face->count && sEyeRingCount[e] < EYE_RING_MAX; i += 3) {  // one corner per triangle
                const float *p = sVerts[face->first + i].pos;
                float dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2];
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d > EYE_RING_INNER && d < EYE_RING_OUTER) sEyeRing[e][sEyeRingCount[e]++] = i;
            }
        }
        return;
    }
    if (face->count != sFaceRestCount) return;

    for (int32_t e = 0; e < 2; e++) {
        if (sEyeRingCount[e] == 0) continue;
        float d[3] = { 0, 0, 0 };
        for (int32_t r = 0; r < sEyeRingCount[e]; r++) {
            int32_t i = sEyeRing[e][r];
            for (int32_t k = 0; k < 3; k++) d[k] += (sObjOut[face->first + i][k] - sFaceRest[i * 3 + k]) / sEyeRingCount[e];
        }
        // Object-space displacement into eye space with the face's rotation
        float de[3];
        for (int32_t k = 0; k < 3; k++) de[k] = d[0] * face->m[0][k] + d[1] * face->m[1][k] + d[2] * face->m[2][k];
        for (int32_t i = 0; i < eyes[e]->count; i++)
            for (int32_t k = 0; k < 3; k++) sVerts[eyes[e]->first + i].pos[k] += de[k];
    }
}

// ---------------------------------------------------------------------------------------- matrices

static void mtx_mul(float res[4][4], const float a[4][4], const float b[4][4]) {
    float tmp[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(res, tmp, sizeof(tmp));
}

// res = M * a (the transpose of the row-vector transform): takes an eye-space direction into model space
static void transposed_mul(float res[3], const float a[3], const float m[4][4]) {
    res[0] = a[0] * m[0][0] + a[1] * m[0][1] + a[2] * m[0][2];
    res[1] = a[0] * m[1][0] + a[1] * m[1][1] + a[2] * m[1][2];
    res[2] = a[0] * m[2][0] + a[1] * m[2][1] + a[2] * m[2][2];
}

static void normalize3(float v[3]) {
    float s = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (s > 0.0f) {
        v[0] /= s; v[1] /= s; v[2] /= s;
    }
}

static void light_dir(const Light_t *l, float out[3]) {
    float d[3] = { l->dir[0] / 127.0f, l->dir[1] / 127.0f, l->dir[2] / 127.0f };
    transposed_mul(out, d, rsp.mv[rsp.mvTop]);
    normalize3(out);
}

// ---------------------------------------------------------------------------------------- RDP state

static int32_t texture_index(uintptr_t addr, int32_t fmt, int32_t siz, int32_t w, int32_t h) {
    for (int32_t i = 0; i < sTextureCount; i++) {
        GdTexture *t = &sTextures[i];
        if (t->addr == addr && t->fmt == fmt && t->siz == siz && t->width == w && t->height == h) return i;
    }
    if (sTextureCount >= MAX_TEXTURES) return -1;
    sTextures[sTextureCount] = (GdTexture) { addr, fmt, siz, w, h };
    return sTextureCount++;
}

static uint8_t cc_color(int32_t slot, uint32_t v) {
    // slot 0 = a, 1 = b, 2 = c, 3 = d
    switch (v) {
        case G_CCMUX_TEXEL0: case G_CCMUX_TEXEL1: return GD_CC_TEXEL0;
        case G_CCMUX_PRIMITIVE: return GD_CC_PRIM;
        case G_CCMUX_SHADE: return GD_CC_SHADE;
        case G_CCMUX_ENVIRONMENT: return GD_CC_ENV;
    }
    if (slot == 0 && v == G_CCMUX_1) return GD_CC_ONE;
    if (slot == 3 && v == 6) return GD_CC_ONE;  // d: 6 = 1, 7 = 0
    if (slot == 2) {
        switch (v) {
            case G_CCMUX_TEXEL0_ALPHA: case G_CCMUX_TEXEL1_ALPHA: return GD_CC_TEXEL0_ALPHA;
            case G_CCMUX_PRIMITIVE_ALPHA: return GD_CC_PRIM_ALPHA;
            case G_CCMUX_SHADE_ALPHA: return GD_CC_SHADE_ALPHA;
            case G_CCMUX_ENV_ALPHA: return GD_CC_ENV_ALPHA;
        }
    }
    return GD_CC_ZERO;
}

static uint8_t cc_alpha(int32_t slot, uint32_t v) {
    switch (v) {
        case G_ACMUX_TEXEL0: case G_ACMUX_TEXEL1: return GD_CC_TEXEL0;
        case G_ACMUX_PRIMITIVE: return GD_CC_PRIM;
        case G_ACMUX_SHADE: return GD_CC_SHADE;
        case G_ACMUX_ENVIRONMENT: return GD_CC_ENV;
        case 6: return slot == 2 ? GD_CC_ZERO : GD_CC_ONE;  // c: PRIM_LOD_FRAC
    }
    return GD_CC_ZERO;
}

static void current_batch_state(GdBatch *b, bool screen) {
    memset(b, 0, sizeof(*b));
    uint32_t w0 = rsp.combineW0, w1 = rsp.combineW1;
    b->colorCombine[0] = cc_color(0, (w0 >> 20) & 0xF);
    b->colorCombine[1] = cc_color(1, (w1 >> 28) & 0xF);
    b->colorCombine[2] = cc_color(2, (w0 >> 15) & 0x1F);
    b->colorCombine[3] = cc_color(3, (w1 >> 15) & 0x7);
    b->alphaCombine[0] = cc_alpha(0, (w0 >> 12) & 0x7);
    b->alphaCombine[1] = cc_alpha(1, (w1 >> 12) & 0x7);
    b->alphaCombine[2] = cc_alpha(2, (w0 >> 9) & 0x7);
    b->alphaCombine[3] = cc_alpha(3, (w1 >> 9) & 0x7);
    memcpy(b->prim, rsp.prim, 4);
    memcpy(b->env, rsp.env, 4);

    uint32_t l = rsp.otherModeL;
    uint32_t flags = 0;
    if (screen) flags |= GD_BATCH_SCREEN;
    if ((l & (G_BL_A_MEM << 18)) == 0) flags |= GD_BATCH_BLEND;
    if ((l & CVG_X_ALPHA) == CVG_X_ALPHA) flags |= GD_BATCH_CUTOUT;
    if (!screen) {
        if (l & Z_CMP) flags |= GD_BATCH_ZTEST;
        if (l & Z_UPD) flags |= GD_BATCH_ZWRITE;
        if ((l & ZMODE_DEC) == ZMODE_DEC) flags |= GD_BATCH_DECAL;
        if (rsp.geometryMode & G_CULL_BACK) flags |= GD_BATCH_CULL_BACK;
        if (rsp.geometryMode & G_CULL_FRONT) flags |= GD_BATCH_CULL_FRONT;
    }

    b->texture = -1;
    if (rsp.texOn || screen) {
        int32_t w = ((rsp.tile[0].lrs - rsp.tile[0].uls) >> 2) + 1;
        int32_t h = ((rsp.tile[0].lrt - rsp.tile[0].ult) >> 2) + 1;
        b->texture = texture_index(rsp.loadedAddr, rsp.tile[0].fmt, rsp.tile[0].siz, w, h);
        flags |= GD_BATCH_TEXTURED;
        b->clampS = (rsp.tile[0].cms & G_TX_CLAMP) != 0;
        b->clampT = (rsp.tile[0].cmt & G_TX_CLAMP) != 0;
        b->mirrorS = (rsp.tile[0].cms & G_TX_MIRROR) != 0;
        b->mirrorT = (rsp.tile[0].cmt & G_TX_MIRROR) != 0;
    }
    b->flags = flags;
}

static GdBatch *batch_for_state(bool screen) {
    GdBatch state;
    current_batch_state(&state, screen);
    if (sBatchCount > 0) {
        GdBatch *last = &sBatches[sBatchCount - 1];
        state.firstVertex = last->firstVertex;
        state.vertexCount = last->vertexCount;
        if (memcmp(&state, last, sizeof(state)) == 0) return last;
    }
    if (sBatchCount >= MAX_BATCHES) return NULL;
    GdBatch *b = &sBatches[sBatchCount++];
    *b = state;
    b->firstVertex = sVertCount;
    b->vertexCount = 0;
    return b;
}

static void emit(GdBatch *b, const float pos[3], const float obj[3], float u, float v, const uint8_t color[4]) {
    if (sVertCount >= MAX_VERTS) return;
    if (obj != NULL) memcpy(sObjOut[sVertCount], obj, sizeof(float) * 3);
    GdVertex *o = &sVerts[sVertCount++];
    memcpy(o->pos, pos, sizeof(o->pos));
    if (b->texture >= 0) {
        const GdTexture *t = &sTextures[b->texture];
        u = (u - rsp.tile[0].uls / 4.0f) / t->width;
        v = (v - rsp.tile[0].ult / 4.0f) / t->height;
    }
    o->uv[0] = u;
    o->uv[1] = v;
    memcpy(o->color, color, 4);
    b->vertexCount++;
}

// ---------------------------------------------------------------------------------------- RSP commands

static void gfx_matrix(uint32_t params, const Mtx *m) {
    if (params & G_MTX_PROJECTION) {
        if (params & G_MTX_LOAD) {
            memcpy(rsp.persp, m->m, sizeof(rsp.persp));
            if (m->m[3][3] == 0.0f) memcpy(rsp.headPersp, m->m, sizeof(rsp.headPersp));
            memset(rsp.view, 0, sizeof(rsp.view));
            rsp.view[0][0] = rsp.view[1][1] = rsp.view[2][2] = rsp.view[3][3] = 1.0f;
        } else {
            mtx_mul(rsp.view, m->m, rsp.view);
        }
    } else {
        if ((params & G_MTX_PUSH) && rsp.mvTop < MTX_STACK - 1) {
            memcpy(rsp.mv[rsp.mvTop + 1], rsp.mv[rsp.mvTop], sizeof(rsp.mv[0]));
            rsp.mvTop++;
        }
        if (params & G_MTX_LOAD) memcpy(rsp.mv[rsp.mvTop], m->m, sizeof(rsp.mv[0]));
        else mtx_mul(rsp.mv[rsp.mvTop], m->m, rsp.mv[rsp.mvTop]);
        rsp.lightsChanged = true;
    }
}

static void gfx_vertex(int32_t n, int32_t dest, const Vtx *vertices) {
    float (*mv)[4] = rsp.mv[rsp.mvTop];
    for (int32_t r = 0; r < 3; r++)
        for (int32_t c = 0; c < 3; c++)
            sComposed[r][c] = mv[r][0] * rsp.view[0][c] + mv[r][1] * rsp.view[1][c] + mv[r][2] * rsp.view[2][c];
    for (int32_t i = 0; i < n && dest + i < 16; i++) {
        const Vtx_t *v = &vertices[i].v;
        const Vtx_tn *vn = &vertices[i].n;
        RspVtx *d = &rsp.vtx[dest + i];
        float x = v->ob[0], y = v->ob[1], z = v->ob[2];
        float w[3], (*vw)[4] = rsp.view, (*pr)[4] = rsp.persp;
        w[0] = x * mv[0][0] + y * mv[1][0] + z * mv[2][0] + mv[3][0];
        w[1] = x * mv[0][1] + y * mv[1][1] + z * mv[2][1] + mv[3][1];
        w[2] = x * mv[0][2] + y * mv[1][2] + z * mv[2][2] + mv[3][2];
        for (int32_t k = 0; k < 3; k++) d->eye[k] = w[0] * vw[0][k] + w[1] * vw[1][k] + w[2] * vw[2][k] + vw[3][k];
        d->obj[0] = x; d->obj[1] = y; d->obj[2] = z;
        for (int32_t k = 0; k < 4; k++) d->clip[k] = d->eye[0] * pr[0][k] + d->eye[1] * pr[1][k] + d->eye[2] * pr[2][k] + pr[3][k];

        int32_t U = (int32_t) ((v->tc[0] * rsp.texScaleS) >> 16);
        int32_t V = (int32_t) ((v->tc[1] * rsp.texScaleT) >> 16);

        if (rsp.geometryMode & G_LIGHTING) {
            if (rsp.lightsChanged) {
                for (int32_t l = 0; l < rsp.numLights - 1; l++) light_dir(&rsp.lights[l], rsp.lightCoeffs[l]);
                light_dir(&rsp.lookat[0], rsp.lookatCoeffs[0]);
                light_dir(&rsp.lookat[1], rsp.lookatCoeffs[1]);
                rsp.lightsChanged = false;
            }
            const Light_t *amb = &rsp.lights[rsp.numLights - 1];
            float r = amb->col[0], g = amb->col[1], b = amb->col[2];
            for (int32_t l = 0; l < rsp.numLights - 1; l++) {
                float intensity = (vn->n[0] * rsp.lightCoeffs[l][0] + vn->n[1] * rsp.lightCoeffs[l][1]
                                   + vn->n[2] * rsp.lightCoeffs[l][2]) / 127.0f;
                if (intensity > 0.0f) {
                    r += intensity * rsp.lights[l].col[0];
                    g += intensity * rsp.lights[l].col[1];
                    b += intensity * rsp.lights[l].col[2];
                }
            }
            d->color[0] = r > 255.0f ? 255 : (uint8_t) r;
            d->color[1] = g > 255.0f ? 255 : (uint8_t) g;
            d->color[2] = b > 255.0f ? 255 : (uint8_t) b;

            if (rsp.geometryMode & G_TEXTURE_GEN) {
                float dotx = vn->n[0] * rsp.lookatCoeffs[0][0] + vn->n[1] * rsp.lookatCoeffs[0][1]
                             + vn->n[2] * rsp.lookatCoeffs[0][2];
                float doty = vn->n[0] * rsp.lookatCoeffs[1][0] + vn->n[1] * rsp.lookatCoeffs[1][1]
                             + vn->n[2] * rsp.lookatCoeffs[1][2];
                if (rsp.geometryMode & G_TEXTURE_GEN_LINEAR) {
                    dotx = acosf(-dotx / 127.0f) / 4.0f;
                    doty = acosf(-doty / 127.0f) / 4.0f;
                } else {
                    dotx = (dotx / 127.0f + 1.0f) / 4.0f;
                    doty = (doty / 127.0f + 1.0f) / 4.0f;
                }
                U = (int32_t) (dotx * rsp.texScaleS);
                V = (int32_t) (doty * rsp.texScaleT);
            }
        } else {
            d->color[0] = v->cn[0];
            d->color[1] = v->cn[1];
            d->color[2] = v->cn[2];
        }
        d->color[3] = v->cn[3];
        d->u = U / 32.0f;
        d->v = V / 32.0f;
    }
}

static void gfx_triangle(int32_t a, int32_t b, int32_t c) {
    // Drop triangles wholly beyond the original near or far plane, as the RSP's clipping would (e.g. particles
    // parked at z = -20000). The sides are left to the portal: a face pulled out towards you can leave the
    // N64's view and still be in yours.
    const RspVtx *t[3] = { &rsp.vtx[a & 15], &rsp.vtx[b & 15], &rsp.vtx[c & 15] };
    for (int32_t axis = 2; axis < 3; axis++) {
        int32_t below = 0, above = 0;
        for (int32_t i = 0; i < 3; i++) {
            below += t[i]->clip[axis] < -t[i]->clip[3];
            above += t[i]->clip[axis] > t[i]->clip[3];
        }
        if (below == 3 || above == 3) return;
    }
    GdBatch *batch = batch_for_state(false);
    if (batch == NULL) return;
    int32_t idx[3] = { a, b, c };
    for (int32_t i = 0; i < 3; i++) {
        const RspVtx *v = &rsp.vtx[idx[i] & 15];
        emit(batch, v->eye, v->obj, v->u, v->v, v->color);
    }
}

// Texture rectangle in screen space: xl/yl/xh/yh are 10.2, s/t are 10.5, dsdx/dtdy are 5.10
static void gfx_texrect(int32_t xl, int32_t yl, int32_t xh, int32_t yh, int32_t s, int32_t t, int32_t dsdx, int32_t dtdy) {
    GdBatch *batch = batch_for_state(true);
    if (batch == NULL) return;
    float x0 = xl / 4.0f, y0 = yl / 4.0f, x1 = xh / 4.0f, y1 = yh / 4.0f;
    float u0 = (int16_t) s / 32.0f, v0 = (int16_t) t / 32.0f;
    float u1 = u0 + (x1 - x0) * ((int16_t) dsdx / 1024.0f);
    float v1 = v0 + (y1 - y0) * ((int16_t) dtdy / 1024.0f);
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    float p00[3] = { x0, y0, 0 }, p10[3] = { x1, y0, 0 }, p11[3] = { x1, y1, 0 }, p01[3] = { x0, y1, 0 };
    emit(batch, p00, NULL, u0, v0, white); emit(batch, p10, NULL, u1, v0, white); emit(batch, p11, NULL, u1, v1, white);
    emit(batch, p00, NULL, u0, v0, white); emit(batch, p11, NULL, u1, v1, white); emit(batch, p01, NULL, u0, v1, white);
}

static int32_t sTrisAtDepth[17];

static void run_dl(const Gfx *cmd, int32_t depth) {
    if (depth > 16) return;
    for (;; cmd++) {
        uintptr_t w0 = cmd->words.w0, w1 = cmd->words.w1;
        uint8_t op = (uint8_t) (w0 >> 24);
        switch (op) {
            case (uint8_t) G_MTX:
                gfx_matrix((w0 >> 16) & 0xFF, (const Mtx *) w1);
                break;
            case (uint8_t) G_POPMTX:
                if (rsp.mvTop > 0) rsp.mvTop--;
                rsp.lightsChanged = true;
                break;
            case (uint8_t) G_MOVEMEM: {
                uint32_t index = (w0 >> 16) & 0xFF;
                if (index == G_MV_LOOKATX) memcpy(&rsp.lookat[0], (const void *) w1, sizeof(Light_t));
                else if (index == G_MV_LOOKATY) memcpy(&rsp.lookat[1], (const void *) w1, sizeof(Light_t));
                else if (index >= G_MV_L0 && index <= G_MV_L0 + 14) memcpy(&rsp.lights[(index - G_MV_L0) / 2], (const void *) w1, sizeof(Light_t));
                rsp.lightsChanged = true;
                break;
            }
            case (uint8_t) G_MOVEWORD:
                if ((w0 & 0xFF) == G_MW_NUMLIGHT) {
                    rsp.numLights = (int32_t) (((uint32_t) w1 - 0x80000000U) / 32);
                    if (rsp.numLights < 1) rsp.numLights = 1;
                    if (rsp.numLights > 8) rsp.numLights = 8;
                    rsp.lightsChanged = true;
                }
                break;
            case (uint8_t) G_VTX:
                gfx_vertex((int32_t) ((w0 >> 20) & 0xF) + 1, (int32_t) ((w0 >> 16) & 0xF), (const Vtx *) w1);
                break;
            case (uint8_t) G_TRI1:
                sTrisAtDepth[depth]++;
                gfx_triangle((int32_t) ((w1 >> 16) & 0xFF) / 10, (int32_t) ((w1 >> 8) & 0xFF) / 10, (int32_t) (w1 & 0xFF) / 10);
                break;
            case (uint8_t) G_DL:
                if (((w0 >> 16) & 0xFF) == G_DL_PUSH) {
                    // Each shape is one display list called at depth 2: note its output range and triangle count
                    int32_t first = sVertCount;
                    sTrisAtDepth[depth + 1] = 0;
                    run_dl((const Gfx *) w1, depth + 1);
                    // (particles are 2-triangle lists at this depth too; only the head's parts are of interest)
                    if (depth + 1 == 2 && sTrisAtDepth[2] >= 20 && sShapeCount < 32) {
                        ShapeRange *r = &sShapes[sShapeCount++];
                        r->first = first;
                        r->count = sVertCount - first;
                        r->tris = sTrisAtDepth[2];
                        memcpy(r->m, sComposed, sizeof(r->m));
                    }
                    sTrisAtDepth[depth] += sTrisAtDepth[depth + 1];
                } else {
                    cmd = (const Gfx *) w1 - 1;
                }
                break;
            case (uint8_t) G_ENDDL:
                return;
            case (uint8_t) G_SETGEOMETRYMODE:
                rsp.geometryMode |= (uint32_t) w1;
                break;
            case (uint8_t) G_CLEARGEOMETRYMODE:
                rsp.geometryMode &= ~(uint32_t) w1;
                break;
            case (uint8_t) G_TEXTURE:
                rsp.texScaleS = (uint16_t) (w1 >> 16);
                rsp.texScaleT = (uint16_t) w1;
                rsp.texOn = (w0 & 0xFF) != 0;
                break;
            case (uint8_t) G_SETOTHERMODE_L:
            case (uint8_t) G_SETOTHERMODE_H: {
                uint32_t sft = (w0 >> 8) & 0xFF, len = w0 & 0xFF;
                uint32_t mask = (uint32_t) (((1ULL << len) - 1) << sft);
                uint32_t *mode = op == (uint8_t) G_SETOTHERMODE_L ? &rsp.otherModeL : &rsp.otherModeH;
                *mode = (*mode & ~mask) | ((uint32_t) w1 & mask);
                break;
            }
            case (uint8_t) G_RDPSETOTHERMODE:
                rsp.otherModeH = (uint32_t) (w0 & 0xFFFFFF);
                rsp.otherModeL = (uint32_t) w1;
                break;
            case (uint8_t) G_SETCOMBINE:
                rsp.combineW0 = (uint32_t) (w0 & 0xFFFFFF);
                rsp.combineW1 = (uint32_t) w1;
                break;
            case (uint8_t) G_SETPRIMCOLOR:
            case (uint8_t) G_SETENVCOLOR: {
                uint8_t *c = op == (uint8_t) G_SETPRIMCOLOR ? rsp.prim : rsp.env;
                c[0] = (uint8_t) (w1 >> 24); c[1] = (uint8_t) (w1 >> 16); c[2] = (uint8_t) (w1 >> 8); c[3] = (uint8_t) w1;
                break;
            }
            case (uint8_t) G_SETTIMG:
                rsp.timgAddr = w1;
                break;
            case (uint8_t) G_LOADBLOCK:
            case (uint8_t) G_LOADTILE:
                rsp.loadedAddr = rsp.timgAddr;
                break;
            case (uint8_t) G_SETTILE: {
                int32_t tile = (int32_t) ((w1 >> 24) & 7);
                rsp.tile[tile].fmt = (int32_t) ((w0 >> 21) & 7);
                rsp.tile[tile].siz = (int32_t) ((w0 >> 19) & 3);
                rsp.tile[tile].cmt = (int32_t) ((w1 >> 18) & 3);
                rsp.tile[tile].cms = (int32_t) ((w1 >> 8) & 3);
                break;
            }
            case (uint8_t) G_SETTILESIZE: {
                int32_t tile = (int32_t) ((w1 >> 24) & 7);
                rsp.tile[tile].uls = (uint16_t) ((w0 >> 12) & 0xFFF);
                rsp.tile[tile].ult = (uint16_t) (w0 & 0xFFF);
                rsp.tile[tile].lrs = (uint16_t) ((w1 >> 12) & 0xFFF);
                rsp.tile[tile].lrt = (uint16_t) (w1 & 0xFFF);
                break;
            }
            case (uint8_t) G_TEXRECT: {
                const Gfx *h1 = cmd + 1, *h2 = cmd + 2;
                gfx_texrect((int32_t) ((w1 >> 12) & 0xFFF), (int32_t) (w1 & 0xFFF),
                            (int32_t) ((w0 >> 12) & 0xFFF), (int32_t) (w0 & 0xFFF),
                            (int32_t) ((h1->words.w1 >> 16) & 0xFFFF), (int32_t) (h1->words.w1 & 0xFFFF),
                            (int32_t) ((h2->words.w1 >> 16) & 0xFFFF), (int32_t) (h2->words.w1 & 0xFFFF));
                cmd += 2;
                break;
            }
            default:
                break;  // syncs, scissor, colour/z image, fill colour/rect (screen clears)
        }
    }
}

// ---------------------------------------------------------------------------------------- API

int32_t gd_api_init(void) {
    static void *sPool;
    if (sPool != NULL) return 1;
    // Same budget the game gives LOAD_MARIO_HEAD (doubled for 64-bit), plus the z/frame buffers it lends
    size_t pool = 0xE1000 * 2;
    sPool = malloc(pool);
    void *zbuf = malloc(0x25800 * 2);
    void *fbuf = malloc(0x70800 * 2);
    if (sPool == NULL || zbuf == NULL || fbuf == NULL) return 0;
    gdm_init(sPool, (u32) pool);
    gd_add_to_heap(zbuf, 0x25800 * 2);
    gd_add_to_heap(fbuf, 0x70800 * 2);
    gdm_setup();
    gdm_maketestdl(2);  // 2 = regular Mario head (3 = game over)
    memset(&rsp, 0, sizeof(rsp));
    rsp.numLights = 2;
    return 1;
}

f32 gMarioFaceDragZ;     // read by drag_picked_object (patches/drag_z.patch)
static void update_hand2(void);

// Second hand (patches/two_hands.patch): its own cursor, run through the original pick/drag in a second pass
struct GdControl gMarioFaceCtrl2;
f32 gMarioFaceDragZ2;
s32 gMarioFaceHolding2;
static struct {
    s32 setCursor;
    float x, y;
    s32 a;
    int8_t stickX, stickY, stickZ;
} sHand2;
s32 gMarioFaceHolding;   // set while a picked object is held (patches/holding.patch)

int32_t gd_api_tick(int32_t setCursor, float cursorX, float cursorY, uint16_t buttons, int8_t stickX, int8_t stickY, int8_t stickZ) {
    // Same order as the game: geo_draw_mario_head_goddard copies the pad and builds the DL, then the
    // V-blank callback (gd_vblank) parses input and moves the cursor
    OSContPad pad;
    memset(&pad, 0, sizeof(pad));
    pad.button = buttons;
    pad.stick_x = stickX;
    pad.stick_y = stickY;
    gd_copy_p1_contpad(&pad);

    gMarioFaceHolding = FALSE;
    gMarioFaceHolding2 = FALSE;
    const Gfx *dl = gdm_gettestdl(2);

    sVertCount = 0;
    sBatchCount = 0;
    sShapeCount = 0;
    rsp.mvTop = 0;
    run_dl(dl, 0);
    eyes_follow_skin();

    int32_t sfx = gd_sfx_to_play();


    if (setCursor) {
        gGdCtrl.csrX = (s32) cursorX;
        gGdCtrl.csrY = (s32) cursorY;
    }
    gd_vblank();
    // The z axis is set directly (gd_api_set_drag_z), so a pull follows the hand rather than a stick
    if (!gGdCtrl.dragging) gMarioFaceDragZ = 0.0f;
    update_hand2();
    sCursorX = (float) gGdCtrl.csrX;
    sCursorY = (float) gGdCtrl.csrY;
    return sfx;
}

const GdVertex *gd_api_vertices(int32_t *count) { *count = sVertCount; return sVerts; }
const GdBatch *gd_api_batches(int32_t *count) { *count = sBatchCount; return sBatches; }
void gd_api_projection(float out[16]) { memcpy(out, rsp.headPersp, sizeof(float) * 16); }
void gd_api_cursor(float *x, float *y) { *x = sCursorX; *y = sCursorY; }
int32_t gd_api_holding(void) { return gMarioFaceHolding; }

// The second cursor's input, as parse_p1_controller treats the pad: A starts/ends a drag at the cursor, the
// stick moves the cursor (same dead zone and rate), and the cursor stays inside the screen view
static void update_hand2(void) {
    struct GdControl *c = &gMarioFaceCtrl2;
    if (sHand2.setCursor) {
        c->csrX = (s32) sHand2.x;
        c->csrY = (s32) sHand2.y;
    }
    c->startedDragging = sHand2.a && !c->dragging;
    c->dragging = sHand2.a != 0;
    if (c->startedDragging) {
        c->dragStartX = c->csrX;
        c->dragStartY = c->csrY;
    }
    if (sHand2.stickX >= 6 || sHand2.stickX <= -6) c->csrX += sHand2.stickX * 0.1;
    if (sHand2.stickY >= 6 || sHand2.stickY <= -6) c->csrY -= sHand2.stickY * 0.1;
    if (c->csrX < 16) c->csrX = 16;
    if (c->csrX > 320 - 48) c->csrX = 320 - 48;
    if (c->csrY < 16) c->csrY = 16;
    if (c->csrY > 240 - 32) c->csrY = 240 - 32;
    if (!c->dragging) gMarioFaceDragZ2 = 0.0f;
}

void gd_api_set_hand2(int32_t setCursor, float cursorX, float cursorY, int32_t a, int8_t stickX, int8_t stickY, int8_t stickZ) {
    sHand2.setCursor = setCursor;
    sHand2.x = cursorX;
    sHand2.y = cursorY;
    sHand2.a = a;
    sHand2.stickX = stickX;
    sHand2.stickY = stickY;
    sHand2.stickZ = stickZ;
}

int32_t gd_api_holding2(void) { return gMarioFaceHolding2; }
void gd_api_cursor2(float *x, float *y) { *x = (float) gMarioFaceCtrl2.csrX; *y = (float) gMarioFaceCtrl2.csrY; }
void gd_api_set_drag_z(int32_t hand, float value) {
    if (hand == 0) gMarioFaceDragZ = value;
    else gMarioFaceDragZ2 = value;
}
struct GdTextureSlot { const char *name; uint8_t *data; unsigned int size; int width, height; };
extern const struct GdTextureSlot gd_texture_slots[];
extern const int gd_texture_slot_count;

int32_t gd_api_set_texture(const char *name, const uint8_t *data, int32_t size) {
    for (int i = 0; i < gd_texture_slot_count; i++) {
        const struct GdTextureSlot *s = &gd_texture_slots[i];
        if (strcmp(s->name, name) != 0) continue;
        if ((unsigned int) size != s->size) return 0;
        memcpy(s->data, data, s->size);
        return 1;
    }
    return 0;
}

int32_t gd_api_texture_slot_count(void) { return gd_texture_slot_count; }

const char *gd_api_texture_slot(int32_t index, int32_t *width, int32_t *height) {
    if (index < 0 || index >= gd_texture_slot_count) return NULL;
    *width = gd_texture_slots[index].width;
    *height = gd_texture_slots[index].height;
    return gd_texture_slots[index].name;
}

int32_t gd_api_texture_count(void) { return sTextureCount; }

void gd_api_texture_size(int32_t index, int32_t *width, int32_t *height) {
    *width = sTextures[index].width;
    *height = sTextures[index].height;
}

static uint8_t expand5(uint32_t v) { return (uint8_t) ((v << 3) | (v >> 2)); }

void gd_api_texture_rgba(int32_t index, uint8_t *out) {
    const GdTexture *t = &sTextures[index];
    const uint8_t *src = (const uint8_t *) t->addr;
    int32_t n = t->width * t->height;
    for (int32_t i = 0; i < n; i++) {
        uint8_t *o = out + i * 4;
        if (t->fmt == G_IM_FMT_RGBA && t->siz == G_IM_SIZ_16b) {
            uint32_t c = (src[i * 2] << 8) | src[i * 2 + 1];
            o[0] = expand5((c >> 11) & 31); o[1] = expand5((c >> 6) & 31); o[2] = expand5((c >> 1) & 31);
            o[3] = (c & 1) ? 255 : 0;
        } else if (t->fmt == G_IM_FMT_RGBA && t->siz == G_IM_SIZ_32b) {
            memcpy(o, src + i * 4, 4);
        } else if (t->fmt == G_IM_FMT_IA && t->siz == G_IM_SIZ_8b) {
            uint8_t c = src[i];
            o[0] = o[1] = o[2] = (uint8_t) ((c >> 4) * 17); o[3] = (uint8_t) ((c & 15) * 17);
        } else if (t->fmt == G_IM_FMT_IA && t->siz == G_IM_SIZ_16b) {
            o[0] = o[1] = o[2] = src[i * 2]; o[3] = src[i * 2 + 1];
        } else if (t->fmt == G_IM_FMT_IA && t->siz == G_IM_SIZ_4b) {
            uint8_t c = (i & 1) ? (src[i / 2] & 15) : (src[i / 2] >> 4);
            o[0] = o[1] = o[2] = (uint8_t) ((c >> 1) * 36); o[3] = (c & 1) ? 255 : 0;
        } else if (t->fmt == G_IM_FMT_I && t->siz == G_IM_SIZ_8b) {
            o[0] = o[1] = o[2] = o[3] = src[i];
        } else if (t->fmt == G_IM_FMT_I && t->siz == G_IM_SIZ_4b) {
            uint8_t c = (i & 1) ? (src[i / 2] & 15) : (src[i / 2] >> 4);
            o[0] = o[1] = o[2] = o[3] = (uint8_t) (c * 17);
        } else {
            o[0] = 255; o[1] = 0; o[2] = 255; o[3] = 255;  // unsupported: magenta
        }
    }
}

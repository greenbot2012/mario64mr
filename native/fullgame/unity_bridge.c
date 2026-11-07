/* Host adapter only. The full game's simulation, Goddard and audio remain upstream. */
#include <ultra64.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "unity_bridge.h"
#include "gfx/gfx_cc.h"
#include "cliopts.h"
#include "configfile.h"
#include "unity_options.h"
#include "goddard/gd_main.h"
#include "goddard/gd_types.h"
#include "goddard/objects.h"

#define EXPORT __attribute__((visibility("default")))
#define MAX_FLOATS (2 * 1024 * 1024)
#define MAX_BATCHES 8192
#define MAX_SHADERS 256

struct ShaderProgram { uint32_t id; struct CCFeatures features; };
struct UnityBatch {
    uint32_t shader_id;
    int first_float, float_count, triangle_count;
    uint32_t texture0, texture1;
    int depth_test, depth_write, decal, blend;
    int viewport[4], scissor[4];
    int filter0, wraps0, wrapt0, filter1, wraps1, wrapt1;
};
struct UnityTexture { uint8_t *pixels; int width, height, version, filter, wraps, wrapt; };
static struct ShaderProgram shaders[MAX_SHADERS], *current_shader;
static int shader_count, batch_count, float_count, initialized, bridge_error;
static struct UnityBatch state, batches[MAX_BATCHES];
static float vertices[MAX_FLOATS];
static float projection[16], batch_projections[MAX_BATCHES][16];
static int object_tag, batch_tags[MAX_BATCHES], game_stage;
static float camera_matrix[16], camera_current[16], cursor_depth;
static int mr_geometry;
static struct UnityTexture *textures;
static int texture_count, texture_capacity, selected_tile;
static int16_t audio_samples[4096];
static int audio_count;
static int queued_audio_frames;
static uint16_t buttons;
static int8_t stick_x, stick_y;
static int cursor_enabled;
static float cursor_x = 160, cursor_y = 120;
static uint64_t frame_count;
extern void main_func(void);
extern void produce_one_frame(void);
extern void produce_current_frame(void);

extern int unity_sixty;
static void noop(void) {}
static bool yes(void) { return true; }
static bool no(void) { return false; }
static void window_init(const char *title) { (void)title; }
static void keys(kb_callback_t a, kb_callback_t b, void (*c)(void)) { (void)a; (void)b; (void)c; }
static void loop(void (*tick)(void)) { tick(); }
/* MarioFace (the 60 fps build) shows the game widescreen: the port widens the view and keeps the HUD at the edges */
static void dimensions(uint32_t *w, uint32_t *h) { *w = unity_sixty ? 1280 : 320; *h = unity_sixty ? 720 : 240; }
static double clock_time(void) { return frame_count / 30.0; }
struct GfxWindowManagerAPI window_unity_api = {
    window_init, keys, loop, dimensions, noop, yes, noop, noop, clock_time, noop
};
static void unload(struct ShaderProgram *p) { (void)p; }
static void load(struct ShaderProgram *p) { current_shader = p; state.shader_id = p->id; }
static struct ShaderProgram *lookup(uint32_t id) {
    for (int i=0; i<shader_count; i++) if (shaders[i].id == id) return &shaders[i];
    return NULL;
}
static struct ShaderProgram *create(uint32_t id) {
    if (shader_count == MAX_SHADERS) { bridge_error = 1; return &shaders[0]; }
    struct ShaderProgram *p = &shaders[shader_count++];
    p->id = id; gfx_cc_get_features(id, &p->features); load(p); return p;
}
static void info(struct ShaderProgram *p, uint8_t *n, bool used[2]) {
    *n = p->features.num_inputs;
    used[0] = p->features.used_textures[0]; used[1] = p->features.used_textures[1];
}
static uint32_t new_texture(void) {
    if (texture_count + 1 >= texture_capacity) {
        int capacity = texture_capacity ? texture_capacity * 2 : 256;
        void *p = realloc(textures, sizeof(*textures) * capacity);
        if (!p) { bridge_error = 2; return 0; }
        textures = p;
        memset(textures + texture_capacity, 0, sizeof(*textures) * (capacity - texture_capacity));
        texture_capacity = capacity;
    }
    return ++texture_count;
}
static void select_texture(int tile, uint32_t id) {
    selected_tile = tile;
    struct UnityTexture *texture=id && id<=(uint32_t)texture_count?&textures[id]:NULL;
    // The upstream renderer caches sampler state on the texture object and
    // need not issue set_sampler_parameters again when that texture is rebound.
    int filter=texture?texture->filter:0, s=texture?texture->wraps:0, t=texture?texture->wrapt:0;
    if (tile == 0) { state.texture0=id; state.filter0=filter; state.wraps0=s; state.wrapt0=t; }
    else { state.texture1=id; state.filter1=filter; state.wraps1=s; state.wrapt1=t; }
}
static void upload(const uint8_t *data, int w, int h) {
    uint32_t id = selected_tile ? state.texture1 : state.texture0;
    if (id == 0 || id > (uint32_t)texture_count || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        bridge_error = 3; return;
    }
    struct UnityTexture *t = &textures[id];
    void *p = realloc(t->pixels, (size_t)w * h * 4);
    if (!p) { bridge_error = 2; return; }
    t->pixels = p; t->width = w; t->height = h; t->version++;
    memcpy(p, data, (size_t)w * h * 4);
}
static void sampler(int tile, bool linear, uint32_t s, uint32_t t) {
    uint32_t id=tile?state.texture1:state.texture0;
    if (id && id<=(uint32_t)texture_count) {
        textures[id].filter=linear; textures[id].wraps=s; textures[id].wrapt=t;
    }
    if (!tile) { state.filter0=linear; state.wraps0=s; state.wrapt0=t; }
    else { state.filter1=linear; state.wraps1=s; state.wrapt1=t; }
}
static void depth_test(bool v) { state.depth_test=v; }
static void depth_write(bool v) { state.depth_write=v; }
static void decal(bool v) { state.decal=v; }
static void blend(bool v) { state.blend=v; }
static void viewport(int x,int y,int w,int h) { int v[]={x,y,w,h}; memcpy(state.viewport,v,sizeof(v)); }
static void scissor(int x,int y,int w,int h) { int v[]={x,y,w,h}; memcpy(state.scissor,v,sizeof(v)); }
static void draw(float *data, size_t length, size_t count) {
    if (batch_count == MAX_BATCHES || float_count + length > MAX_FLOATS) { bridge_error=4; return; }
    state.first_float=float_count; state.float_count=length; state.triangle_count=count;
    memcpy(batch_projections[batch_count],projection,sizeof(projection));
    batch_tags[batch_count]=object_tag;
    batches[batch_count++]=state;
    memcpy(vertices+float_count,data,length*sizeof(float)); float_count+=length;
}
static void start_frame(void) { batch_count=float_count=audio_count=0; frame_count++; }
struct GfxRenderingAPI gfx_unity_api = {
    no, unload, load, create, lookup, info, new_texture, select_texture, upload,
    sampler, depth_test, depth_write, decal, viewport, scissor, blend, draw,
    noop, noop, start_frame, noop, noop, noop
};
static int buffered(void) { return queued_audio_frames; }
static int desired_audio(void) { return 1100; }
static void audio_play(const uint8_t *data, size_t length) {
    size_t samples=length/2;
    if (samples > 4096) { bridge_error=5; return; }
    memcpy(audio_samples,data,length); audio_count=samples;
}
struct AudioAPI audio_unity_api = { yes, buffered, desired_audio, audio_play, noop };

void unity_read_controller(OSContPad *pad) {
    pad->button=buttons; pad->stick_x=stick_x; pad->stick_y=stick_y;
}
void unity_read_cursor(void) {
    if (cursor_enabled) { gGdCtrl.csrX=cursor_x; gGdCtrl.csrY=cursor_y; }
}
void unity_read_menu_cursor(float *x, float *y) {
    if (cursor_enabled) { *x=cursor_x-160; *y=120-cursor_y; }
}
float unity_cursor_depth(void) { return cursor_enabled ? cursor_depth : 0; }
void unity_set_stage(int stage) { game_stage=stage; }
void unity_set_tag(int tag) { object_tag=tag; }
/* With 60 fps interpolation the game frame is drawn between the previous camera and this one: the camera of
 * the frame last drawn is the one reported. */
void unity_set_camera(const float *matrix) {
    memcpy(camera_current,matrix,sizeof(camera_current)); memcpy(camera_matrix,matrix,sizeof(camera_matrix));
}
void unity_set_camera_interpolated(const float *matrix) { memcpy(camera_matrix,matrix,sizeof(camera_matrix)); }
void unity_set_projection(const float *matrix) { memcpy(projection,matrix,sizeof(projection)); }
int unity_is_mr(void) { return mr_geometry; }
EXPORT int sm64unity_init(const char *save_path) {
    if (initialized) return 1;
    if (!save_path || strlen(save_path) >= sizeof(gCLIOpts.SavePath)) return 0;
    snprintf(gCLIOpts.SavePath,sizeof(gCLIOpts.SavePath),"%s",save_path);
    main_func(); initialized=1; return bridge_error == 0;
}
EXPORT int sm64unity_tick(uint16_t pad, int8_t x, int8_t y) {
    if (!initialized) return 0;
    buttons=pad; stick_x=x; stick_y=y; unity_apply_options(); produce_one_frame(); return bridge_error == 0;
}
/* 60 fps: sm64unity_tick draws the frame between the last game frame and this one; this draws this one. */
EXPORT int sm64unity_interpolates(void) { return unity_sixty; }
EXPORT int sm64unity_render_current(void) {
    if (!initialized || !unity_sixty) return 0;
    int audio=audio_count; uint64_t frames=frame_count;
    memcpy(camera_matrix,camera_current,sizeof(camera_matrix));
    produce_current_frame();
    audio_count=audio; frame_count=frames;
    return bridge_error == 0;
}
EXPORT void sm64unity_cursor(int enabled, float x, float y) {
    cursor_enabled=enabled; cursor_x=x<0?0:x>319?319:x; cursor_y=y<0?0:y>239?239:y;
}
EXPORT void sm64unity_mr_geometry(int enabled) { mr_geometry=enabled; }
EXPORT void sm64unity_hand_depth(float depth) { cursor_depth=depth; }
EXPORT int sm64unity_stage(void) { return game_stage; }
/* Observe the original face joint for input verification/diagnostics. An id
 * of -1 queries the currently picked joint; this never changes Goddard state. */
EXPORT int sm64unity_face_joint(int id, float *out) {
    if (game_stage != 1 || !out) return 0;
    for (struct GdObj *obj=gGdObjectList; obj; obj=obj->next) {
        if (obj->type != OBJ_TYPE_JOINTS || !(obj->drawFlags & OBJ_IS_GRABBALE)) continue;
        if (id<0 ? !(obj->drawFlags & OBJ_PICKED) : obj->number!=id) continue;
        struct ObjJoint *joint=(struct ObjJoint *)obj;
        out[0]=obj->number; out[1]=(obj->drawFlags & OBJ_PICKED)!=0;
        out[2]=joint->mat128[3][0]-joint->unk54.x;
        out[3]=joint->mat128[3][1]-joint->unk54.y;
        out[4]=joint->mat128[3][2]-joint->unk54.z;
        out[5]=gGdCtrl.csrX; out[6]=gGdCtrl.csrY; out[7]=gGdCtrl.btnApressed;
        return 1;
    }
    return 0;
}
EXPORT const int *sm64unity_tags(void) { return batch_tags; }
EXPORT const float *sm64unity_camera(void) { return camera_matrix; }
EXPORT const float *sm64unity_projections(void) { return &batch_projections[0][0]; }
EXPORT const struct UnityBatch *sm64unity_batches(int *count) { *count=batch_count; return batches; }
EXPORT const float *sm64unity_vertices(int *count) { *count=float_count; return vertices; }
EXPORT const uint8_t *sm64unity_texture(int id,int *w,int *h,int *version) {
    if (id <= 0 || id > texture_count) { *w=*h=*version=0; return NULL; }
    *w=textures[id].width; *h=textures[id].height; *version=textures[id].version; return textures[id].pixels;
}
EXPORT const int16_t *sm64unity_audio(int *count) { *count=audio_count; return audio_samples; }
EXPORT void sm64unity_audio_buffered(int frames) { queued_audio_frames=frames; }
EXPORT int sm64unity_error(void) { return bridge_error; }
EXPORT int sm64unity_batch_size(void) { return sizeof(struct UnityBatch); }

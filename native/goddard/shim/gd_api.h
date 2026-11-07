// libgoddard: SM64's original "Goddard" Mario-head code (src/goddard from the decomp), run as a library.
// Each tick is one game frame (30 Hz on the console): input in, an N64 display list out, which is interpreted
// here (F3D, as the RSP would) into triangles with RSP lighting/texgen applied, batched by RDP state.
#pragma once
#include <stdint.h>

#if defined(_WIN32)
#define GD_API __declspec(dllexport)
#else
#define GD_API __attribute__((visibility("default")))
#endif

// One output vertex. Space depends on the batch: eye space of the Goddard camera (N64 right-handed, camera
// looking down -z) for 3D batches, or screen pixels (320x240, y down, z = 0) for 2D batches.
typedef struct {
    float pos[3];
    float uv[2];       // normalised to the bound texture
    uint8_t color[4];  // shade colour (vertex colour or RSP lighting result), alpha from the vertex
} GdVertex;

enum {
    GD_BATCH_SCREEN = 1 << 0,  // 2D: texture rectangle in screen pixels
    GD_BATCH_BLEND = 1 << 1,   // alpha blended (blender reads the incoming alpha)
    GD_BATCH_CUTOUT = 1 << 2,  // alpha-tested (CVG_X_ALPHA)
    GD_BATCH_ZTEST = 1 << 3,
    GD_BATCH_ZWRITE = 1 << 4,
    GD_BATCH_DECAL = 1 << 5,
    GD_BATCH_CULL_BACK = 1 << 6,
    GD_BATCH_CULL_FRONT = 1 << 7,
    GD_BATCH_TEXTURED = 1 << 8,
};

// Colour combiner inputs, normalised from the RDP's per-slot mux encodings
enum {
    GD_CC_ZERO, GD_CC_ONE, GD_CC_TEXEL0, GD_CC_TEXEL0_ALPHA, GD_CC_PRIM, GD_CC_PRIM_ALPHA,
    GD_CC_SHADE, GD_CC_SHADE_ALPHA, GD_CC_ENV, GD_CC_ENV_ALPHA,
};

typedef struct {
    int32_t firstVertex;   // triangles are unindexed: vertices [firstVertex, firstVertex + vertexCount)
    int32_t vertexCount;
    int32_t texture;       // index into the texture table, -1 if untextured
    uint32_t flags;        // GD_BATCH_*
    uint8_t colorCombine[4];  // (a - b) * c + d, GD_CC_* each
    uint8_t alphaCombine[4];
    uint8_t prim[4];
    uint8_t env[4];
    uint8_t clampS, clampT, mirrorS, mirrorT;
} GdBatch;

GD_API int32_t gd_api_init(void);
// Runs one frame. With setCursor the cursor is placed at (cursorX, cursorY) in Goddard screen pixels
// (0..320, 0..240, y down); otherwise it moves only by the stick, as on the console (which is what keeps a
// drag bounded: the game pulls the cursor back towards the grab point every frame). stickZ is unused (depth
// is set with gd_api_set_drag_z). Buttons are N64 button bits
// (A_BUTTON = 0x8000). Returns the frame's sound flags (SOUND_MENU_FLAG_*) for play_menu_sounds.
GD_API int32_t gd_api_tick(int32_t setCursor, float cursorX, float cursorY, uint16_t buttons, int8_t stickX, int8_t stickY, int8_t stickZ);

GD_API const GdVertex *gd_api_vertices(int32_t *count);
GD_API const GdBatch *gd_api_batches(int32_t *count);
// Perspective projection of the head view (row-major, N64 row-vector convention)
GD_API void gd_api_projection(float out[16]);
// Cursor position after Goddard clamped it to the screen
GD_API void gd_api_cursor(float *x, float *y);
// Whether the last tick's drag is holding part of the head (as opposed to pinching thin air)
GD_API int32_t gd_api_holding(void);
// Depth of the current drag for hand 0 / 1 (towards the camera +), applied by drag_picked_object every frame
// like the x/y cursor offset. Held objects settle ~6 Goddard units out per unit. Reset when the drag ends.
GD_API void gd_api_set_drag_z(int32_t hand, float value);

// Second hand: a second cursor that grabs and drags its own point of the head by the same rules
// (patches/two_hands.patch). Set before each tick; same meaning as gd_api_tick's arguments, A as a bool.
GD_API void gd_api_set_hand2(int32_t setCursor, float cursorX, float cursorY, int32_t a, int8_t stickX, int8_t stickY, int8_t stickZ);
GD_API int32_t gd_api_holding2(void);
GD_API void gd_api_cursor2(float *x, float *y);

// The game's own textures are not in the library: before gd_api_init, give each one (name as in the decomp,
// "hand_open.rgba16") its N64-format bytes. Returns 0 for an unknown name or the wrong size.
GD_API int32_t gd_api_set_texture(const char *name, const uint8_t *data, int32_t size);
GD_API int32_t gd_api_texture_slot_count(void);
GD_API const char *gd_api_texture_slot(int32_t index, int32_t *width, int32_t *height);
GD_API int32_t gd_api_texture_count(void);
GD_API void gd_api_texture_size(int32_t index, int32_t *width, int32_t *height);
// Decodes to RGBA8, width * height * 4 bytes, top row first
GD_API void gd_api_texture_rgba(int32_t index, uint8_t *out);

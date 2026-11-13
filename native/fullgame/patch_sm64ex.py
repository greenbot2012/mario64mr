"""
Mario 64 MR's changes to the sm64ex PC port (at REVISION), applied to a checkout of it before the game library is
built (install_mario64mr.py): the Unity bridge (unity_*.c/h, next to this file) in place of SDL, and small hooks into
the game for mixed reality (Mario carried out into your room, hands in the game, the camera held still, a wider
sky...). Every change is made from the pinned upstream file, so running it again is safe.
"""
import re
import shutil
import struct
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
REVISION = "d7ca2c04364a6dd0dac58b47151e04e26887e6f0"


# The options menu's own words (unity_options.inc.h): menu titles in the HUD's capitals
MR_STRINGS = """
#define TEXT_MR_GRAPHICS     _("Graphics Settings")
#define TEXT_MR_BRING_BACK   _("Reset Mario")
#define TEXT_MR_REFRESH      _("Refresh Rate")
#define TEXT_MR_RESOLUTION   _("Resolution")
#define TEXT_MR_ANTIALIASING _("Anti-aliasing")
#define TEXT_MR_FOVEATION    _("Foveation")
#define TEXT_MR_TEXTURES     _("Textures")
#define TEXT_MR_SHARPENING   _("Sharpening")
#define TEXT_MR_DEFAULTS     _("Back to defaults")
#define TEXT_MR_72HZ    _("72 Hz")
#define TEXT_MR_80HZ    _("80 Hz")
#define TEXT_MR_90HZ    _("90 Hz")
#define TEXT_MR_120HZ   _("120 Hz")
#define TEXT_MR_70      _("70%")
#define TEXT_MR_80      _("80%")
#define TEXT_MR_90      _("90%")
#define TEXT_MR_100     _("100%")
#define TEXT_MR_110     _("110%")
#define TEXT_MR_120     _("120%")
#define TEXT_MR_130     _("130%")
#define TEXT_MR_140     _("140%")
#define TEXT_MR_150     _("150%")
#define TEXT_MR_OFF     _("Off")
#define TEXT_MR_MSAA2   _("MSAA 2x")
#define TEXT_MR_MSAA4   _("MSAA 4x")
#define TEXT_MR_LOW     _("Low")
#define TEXT_MR_MEDIUM  _("Medium")
#define TEXT_MR_HIGH    _("High")
#define TEXT_MR_HIGHTOP _("High (top)")
#define TEXT_MR_N64     _("OG N64 (unfiltered)")
#define TEXT_MR_MIPMAPS _("Mipmaps")
#define TEXT_MR_ANISO2  _("Anisotropic 2x")
#define TEXT_MR_ANISO4  _("Anisotropic 4x")
#define TEXT_MR_ANISO8  _("Anisotropic 8x")
#define TEXT_MR_ANISO16 _("Anisotropic 16x")
#define TEXT_MR_NORMAL  _("Normal")
#define TEXT_MR_QUALITY _("Quality")
"""


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Upstream source changed: " + old[:80])
    return text.replace(old, new, 1)


SIXTY_PATCH = "enhancements/60fps_ex.patch"


def prepare(root, rom, sixty=True, external_data=True):
    if subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip() != REVISION:
        raise ValueError("Unexpected runtime revision")
    # The PC port's own 60 fps interpolation: every game frame is drawn twice, first between the previous
    # frame and this one, then as it is. Restore what it touches, then apply it or not.
    patched = [line[6:] for line in (root / SIXTY_PATCH).read_text().splitlines() if line.startswith("+++ b/")]
    subprocess.run(["git", "checkout", "HEAD", "--"] + patched, cwd=root, check=True)
    if sixty:
        subprocess.run(["git", "apply", SIXTY_PATCH], cwd=root, check=True)

    # Always derive patched files from the pinned commit (with the 60 fps patch if wanted), making re-runs safe.
    def original(path):
        if path in patched:
            return (root / path).read_text()
        return subprocess.check_output(["git", "show", f"HEAD:{path}"], cwd=root, text=True)

    # Apple's bundled GNU Make 3.81 predates the != shell assignment. Preserve
    # upstream setup commands while expressing them with the older syntax.
    makefile = original("Makefile")
    import re
    makefile = re.sub(r"^(\w+) != (.*)$", r"\1 := $(shell \2)", makefile, flags=re.M)
    # Omit the PC fork's nightly watermark from the original presentation.
    makefile = makefile.replace('ifeq ($(shell git rev-parse --abbrev-ref HEAD),nightly)', 'ifeq (0,1)')
    (root / "Makefile").write_text(makefile)
    split = re.sub(r"^define (\w+) =[^\S\n]*$", r"define \1", original("Makefile.split"), flags=re.M)
    (root / "Makefile.split").write_text(split)

    main = original("src/pc/pc_main.c")
    main = main.replace('#include "pc_main.h"', '#include "pc_main.h"\n#include "unity_bridge.h"')
    main = replace_once(main, '#if defined(WAPI_SDL1) || defined(WAPI_SDL2)',
                        '#if defined(WAPI_UNITY)\n    wm_api = &window_unity_api;\n    #elif defined(WAPI_SDL1) || defined(WAPI_SDL2)')
    main = replace_once(main, '#if defined(RAPI_D3D11)',
                        '#if defined(RAPI_UNITY)\n    rendering_api = &gfx_unity_api;\n    #define RAPI_NAME "Unity"\n    #elif defined(RAPI_D3D11)')
    main = replace_once(main, '    if (audio_api == NULL) {\n        audio_api = &audio_null;',
                        '    audio_api = &audio_unity_api;\n    if (audio_api == NULL) {\n        audio_api = &audio_null;')
    main = replace_once(main, '#ifdef TARGET_WEB\n    emscripten_set_main_loop',
                        '#ifdef WAPI_UNITY\n    return;\n#elif defined(TARGET_WEB)\n    emscripten_set_main_loop')
    # The host draws the two frames at its own times: the in-between one with the game frame, this one after
    if sixty:
        main = replace_once(main, '    gfx_end_frame();\n\n    gfx_start_frame();\n    patch_interpolations();',
                            '    gfx_end_frame();\n}\n\nint unity_sixty = 1;\nvoid produce_current_frame(void) {\n'
                            '    gfx_start_frame();\n    patch_interpolations();')
    else:
        main += '\nint unity_sixty = 0;\nvoid produce_current_frame(void) {}\n'
    (root / "src/pc/pc_main.c").write_text(main)
    controller = original("src/pc/controller/controller_entry_point.c")
    controller = '#include "pc/unity_bridge.h"\n' + controller
    # Header needs the platform controller definitions first.
    controller = controller.replace('#include "pc/unity_bridge.h"\n', '#include <ultra64.h>\n#include "pc/unity_bridge.h"\n')
    controller = replace_once(controller, '    pad->errnum = 0;', '    pad->errnum = 0;\n    unity_read_controller(pad);')
    (root / "src/pc/controller/controller_entry_point.c").write_text(controller)
    renderer = original("src/goddard/renderer.c")
    # Feed an absolute fingertip/cursor position immediately before the original
    # picking/eye-update code consumes the parsed controller input.
    renderer = replace_once(renderer, 'void parse_p1_controller(void) {',
                             'extern void unity_read_cursor(void);\nvoid parse_p1_controller(void) {')
    # Cursor is overridden at the end of parsing, after stick integration.
    start = renderer.index('void parse_p1_controller(void) {')
    end = renderer.index('\n}\n', start)
    renderer = renderer[:end] + '\n    unity_read_cursor();' + renderer[end:]
    (root / "src/goddard/renderer.c").write_text(renderer)
    objects = original("src/goddard/objects.c")
    objects = 'extern float unity_cursor_depth(void);\n' + objects
    objects = replace_once(objects, '    spD0.z = 0.0f;', '    spD0.z = unity_cursor_depth();')
    (root / "src/goddard/objects.c").write_text(objects)
    menu = original("src/menu/file_select.c")
    menu = 'extern void unity_read_menu_cursor(float *, float *);\nextern void unity_set_stage(int);\n' + menu
    menu = replace_once(menu, '    // Stop cursor from going offscreen',
                        '    unity_read_menu_cursor(&sCursorPos[0], &sCursorPos[1]);\n\n    // Stop cursor from going offscreen')
    start = menu.index('{', menu.index('s32 lvl_init_menu_values_and_cursor_pos('))
    menu = menu[:start+1] + '\n    unity_set_stage(2);' + menu[start+1:]
    (root / "src/menu/file_select.c").write_text(menu)
    intro = original("src/menu/level_select_menu.c")
    intro = 'extern void unity_set_stage(int);\n' + intro
    intro = replace_once(intro, 's32 lvl_intro_update(s16 arg1, UNUSED s32 arg2) {',
                         's32 lvl_intro_update(s16 arg1, UNUSED s32 arg2) {\n    unity_set_stage(arg1 == 0 ? 0 : 1);')
    if external_data:  # no attract demos: their inputs are not in the library (main())
        intro = replace_once(intro, "if ((++gDemoCountdown) == PRESS_START_DEMO_TIMER) {",
                             "if (0 && (++gDemoCountdown) == PRESS_START_DEMO_TIMER) {")
    (root / "src/menu/level_select_menu.c").write_text(intro)
    level = original("src/game/level_update.c")
    level = 'extern void unity_set_stage(int);\n' + level
    start = level.index('{', level.index('s32 lvl_init_from_save_file('))
    level = level[:start+1] + '\n    unity_set_stage(3);' + level[start+1:]
    area = original("src/game/area.c")
    if sixty:
        # (for MarioFace) The HUD (its icons, then its numbers, printed as text labels) is tagged 3, so the host can
        # set it at the window's edges
        tag = '        gDisplayListHead->words.w0 = 0xD2000000;\n        (gDisplayListHead++)->words.w1 = %d;\n'
        area = replace_once(area, '        render_hud();\n\n        gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);\n        render_text_labels();\n',
                            tag % 3 + '        render_hud();\n\n        gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);\n        render_text_labels();\n' + tag % 0)
    (root / "src/game/area.c").write_text(area)
    acts = original("src/menu/star_select.c")
    if sixty:
        # (for MarioFace) The act select (the stars of a course) is a menu of its own, stage 4; the level is
        # stage 3 again whenever one starts
        start = level.index('{', level.index('s32 init_level(void) {'))
        level = level[:start+1] + '\n    unity_set_stage(3);' + level[start+1:]
        acts = 'extern void unity_set_stage(int);\n' + acts
        start = acts.index('{', acts.index('s32 lvl_init_act_selector_values_and_stars('))
        acts = acts[:start+1] + '\n    unity_set_stage(4);' + acts[start+1:]
    (root / "src/game/level_update.c").write_text(level)
    (root / "src/menu/star_select.c").write_text(acts)
    gfx = original("src/pc/gfx/gfx_pc.c")
    gfx = 'extern void unity_set_projection(const float *);\nextern void unity_set_tag(int);\nextern int unity_is_mr(void);\n' + gfx
    gfx = replace_once(gfx, '            // RSP commands:',
                       '            case 0xD2: /* Unity object identity; no change to original geometry. */\n                gfx_flush(); unity_set_tag(cmd->words.w1); break;\n            // RSP commands:')
    gfx = replace_once(gfx, '    if (parameters & G_MTX_PROJECTION) {',
                       '    if (parameters & G_MTX_PROJECTION) {\n        gfx_flush();')
    # Goddard multiplies its camera into the projection stack. Retain the
    # loaded lens matrix separately so unprojection preserves that camera.
    gfx = replace_once(gfx, '            memcpy(rsp.P_matrix, matrix, sizeof(matrix));',
                       '            memcpy(rsp.P_matrix, matrix, sizeof(matrix));\n            unity_set_projection(&matrix[0][0]);')
    # In MR the viewer is free to lean. Culling against the original camera
    # would remove geometry visible to one of the tracked eyes.
    gfx = replace_once(gfx, '    if (v1->clip_rej & v2->clip_rej & v3->clip_rej) {',
                       '    if (!unity_is_mr() && (v1->clip_rej & v2->clip_rej & v3->clip_rej)) {')
    gfx = replace_once(gfx, '    if ((rsp.geometry_mode & G_CULL_BOTH) != 0) {',
                       '    if (!unity_is_mr() && (rsp.geometry_mode & G_CULL_BOTH) != 0) {')
    (root / "src/pc/gfx/gfx_pc.c").write_text(gfx)
    graph_h = original("src/engine/graph_node.h")
    graph_h = replace_once(graph_h, '    struct DisplayListNode *next;',
                           '    struct DisplayListNode *next;\n    int unityTag;')
    (root / "src/engine/graph_node.h").write_text(graph_h)
    graph = original("src/game/rendering_graph_node.c")
    graph = '#include "game/object_list_processor.h"\nextern int unity_is_mr(void);\nextern void unity_set_camera(const float *);\nextern void unity_set_camera_interpolated(const float *);\nstatic int unity_shadow_tag;\n' + graph
    graph = replace_once(graph, '        listNode->displayList = displayList;',
                         '        listNode->displayList = displayList;\n        listNode->unityTag = gMarioObject && gCurGraphNodeObject == &gMarioObject->header.gfx ? (unity_shadow_tag ? 2 : 1) : 0;')
    tag_begin = '                gDisplayListHead->words.w0 = 0xD2000000;\n                (gDisplayListHead++)->words.w1 = currList->unityTag;\n'
    tag_end = '\n                gDisplayListHead->words.w0 = 0xD2000000;\n                (gDisplayListHead++)->words.w1 = 0;'
    if sixty:
        # (the matrix and list the 60 fps patch rewrites in place stay together, between the tags)
        graph = replace_once(graph, '                if ((u32) gMtxTblSize < sizeof(gMtxTbl) / sizeof(gMtxTbl[0])) {',
                             tag_begin + '                if ((u32) gMtxTblSize < sizeof(gMtxTbl) / sizeof(gMtxTbl[0])) {')
        graph = replace_once(graph, '                gSPDisplayList(gDisplayListHead++, currList->displayListInterpolated);',
                             '                gSPDisplayList(gDisplayListHead++, currList->displayListInterpolated);' + tag_end)
        graph = replace_once(graph, '    mtxf_lookat(cameraTransform, posInterpolated, focusInterpolated, node->roll);',
                             '    unity_camera_further(posInterpolated, focusInterpolated);\n'
                             '    mtxf_lookat(cameraTransform, posInterpolated, focusInterpolated, node->roll);\n'
                             '    unity_set_camera_interpolated(&cameraTransform[0][0]);')
    else:
        graph = replace_once(graph, '                gSPDisplayList(gDisplayListHead++, currList->displayList);',
                             tag_begin + '                gSPDisplayList(gDisplayListHead++, currList->displayList);' + tag_end)
    graph = replace_once(graph, '    mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);',
                         '    { Vec3f unityPos; vec3f_copy(unityPos, node->pos); unity_camera_further(unityPos, node->focus);\n'
                         '    mtxf_lookat(cameraTransform, unityPos, node->focus, node->roll); }\n    unity_set_camera(&cameraTransform[0][0]);')
    graph = replace_once(graph, '    geo = node->sharedChild;',
                         '    if (unity_is_mr()) return TRUE;\n    geo = node->sharedChild;')
    graph = 'extern void unity_camera_further(float *pos, const float *focus);\nextern int unity_mario_outside(void);\nextern int unity_camera_frozen(void);\nextern void unity_room_begin(void);\nextern void unity_room_end(void);\n' + graph
    graph = replace_once(graph, '    if (node->fnNode.func != NULL) {\n        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);\n    }\n    mtxf_rotate_xy',
                         '    if (node->fnNode.func != NULL && !unity_camera_frozen()) {\n        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);\n    }\n    mtxf_rotate_xy')
    shadow_call = '        shadowList = create_shadow_below_xyz(shadowPos[0], shadowPos[1], shadowPos[2], shadowScale,\n                                             node->shadowSolidity, node->shadowType);'
    room_begin = '        int roomShadow = unity_mario_outside() && gMarioObject && gCurGraphNodeObject == &gMarioObject->header.gfx;\n        if (roomShadow) unity_room_begin();\n'
    if sixty:
        graph = replace_once(graph, '        extern u8 gInterpolatingSurfaces;\n', room_begin + '        extern u8 gInterpolatingSurfaces;\n')
    else:
        graph = replace_once(graph, shadow_call, room_begin + shadow_call)
    graph = replace_once(graph, shadow_call, shadow_call + '\n        if (roomShadow) unity_room_end();')
    # Keep the original shadow vertices/texture and floor fitting, but identify
    # the shadow separately so Unity never rotates it with a carried body.
    graph = replace_once(graph, '            if (gShadowAboveWaterOrLava == 1) {',
                         '            unity_shadow_tag = 1;\n            if (gShadowAboveWaterOrLava == 1) {')
    last_shadow = ('                geo_append_display_list2((void *) VIRTUAL_TO_PHYSICAL(shadowList),\n'
                   '                                         (void *) VIRTUAL_TO_PHYSICAL(shadowListInterpolated), 6);\n            }'
                   if sixty else '                geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowList), 6);\n            }')
    graph = replace_once(graph, last_shadow, last_shadow + '\n            unity_shadow_tag = 0;')
    # Carried from one world into the other Mario jumps: his next frame is not drawn halfway between
    graph += ('\nvoid unity_skip_interpolation(void) {\n    if (gMarioObject) gMarioObject->header.gfx.skipInterpolationTimestamp = gGlobalTimer;\n}\n'
              if sixty else '\nvoid unity_skip_interpolation(void) {}\n')
    (root / "src/game/rendering_graph_node.c").write_text(graph)
    processor = original("src/game/object_list_processor.c")
    processor = replace_once(processor, '    particleFlags = execute_mario_action(gCurrentObject);',
                              '    extern s32 unity_execute_mario_action(struct Object *);\n    particleFlags = unity_execute_mario_action(gCurrentObject);')
    # Hands reaching into the game are moving surfaces like the game's own, loaded right after it clears them
    processor = replace_once(processor, '    clear_dynamic_surfaces();\n\n    // Update spawners',
                             '    clear_dynamic_surfaces();\n    extern void unity_load_game_hands(void);\n'
                             '    unity_load_game_hands();\n\n    // Update spawners')
    (root / "src/game/object_list_processor.c").write_text(processor)
    camera = original("src/game/camera.c")
    camera = 'extern int unity_camera_frozen(void);\n' + camera
    camera = replace_once(camera, 'void update_camera(struct Camera *c) {',
                          'void update_camera(struct Camera *c) {\n    unity_camera_ease();\n    if (unity_camera_frozen()) return;\n'
                          '    if (unity_camera_reset_due()) {\n'
                          '        s32 zoomedOut = gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT;\n'
                          '        reset_camera(c);\n'
                          '        gCameraMovementFlags |= zoomedOut;\n'
                          '    }')
    # In MR, C-down past the game's zoomed-out step keeps going further (unity_mr.c), and C-up comes back
    # through those steps before the game's own zoom in
    camera = 'extern int unity_camera_reset_due(void);\nextern void unity_camera_ease(void);\nextern int unity_zoom_further(void);\nextern int unity_zoom_back(void);\n' + camera
    camera = replace_once(camera, '    // Zoom in\n    if (gPlayer1Controller->buttonPressed & U_CBUTTONS) {\n        if (c->mode != CAMERA_MODE_FIXED && (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT)) {',
                          '    // Zoom in\n    if ((gPlayer1Controller->buttonPressed & U_CBUTTONS) && c->mode != CAMERA_MODE_FIXED && unity_zoom_back()) {\n'
                          '        play_sound_cbutton_up();\n    } else if (gPlayer1Controller->buttonPressed & U_CBUTTONS) {\n'
                          '        if (c->mode != CAMERA_MODE_FIXED && (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT)) {')
    camera = replace_once(camera, '                gCameraMovementFlags |= CAM_MOVE_ALREADY_ZOOMED_OUT;\n                sZoomAmount = gCameraZoomDist + 400.f;\n#ifndef VERSION_JP\n                play_camera_buzz_if_cdown();\n#endif',
                          '                gCameraMovementFlags |= CAM_MOVE_ALREADY_ZOOMED_OUT;\n                sZoomAmount = gCameraZoomDist + 400.f;\n'
                          '                if (unity_zoom_further()) play_sound_cbutton_down();\n#ifndef VERSION_JP\n                else play_camera_buzz_if_cdown();\n#endif')
    (root / "src/game/camera.c").write_text(camera)
    # A float angle out of range (a Piranha Plant bubble dividing by an animation's length of 0, after its plant
    # changed animation) made (u16) of it undefined: arm64 saturates to 0xFFFFFFFF and indexes a gigabyte past the
    # table. Through s32 it saturates, then wraps to 16 bits as the N64 did.
    math_util = original("src/engine/math_util.h")
    math_util = replace_once(math_util, '#define sins(x) gSineTable[(u16) (x) >> 4]', '#define sins(x) gSineTable[(u16) (s32) (x) >> 4]')
    math_util = replace_once(math_util, '#define coss(x) gCosineTable[(u16) (x) >> 4]', '#define coss(x) gCosineTable[(u16) (s32) (x) >> 4]')
    (root / "src/engine/math_util.h").write_text(math_util)
    bubbles = original("src/game/behaviors/piranha_bubbles.inc.c")
    bubbles = replace_once(bubbles, '                    if (frame < doneShrinkingFrame) {',
                           '                    if (doneShrinkingFrame < 1.0f) {\n'
                           '                        scale = 1.0f; // (not the sleeping animation: nothing to time it by)\n'
                           '                    } else if (frame < doneShrinkingFrame) {')
    (root / "src/game/behaviors/piranha_bubbles.inc.c").write_text(bubbles)
    interaction = original("src/game/interaction.c")
    interaction = 'extern int unity_mario_outside(void);\n' + interaction
    interaction = replace_once(interaction, 'void mario_process_interactions(struct MarioState *m) {',
                               'void mario_process_interactions(struct MarioState *m) {\n    if (unity_mario_outside()) { m->marioObj->oInteractStatus=0; return; }')
    (root / "src/game/interaction.c").write_text(interaction)
    # Levels split into rooms (the castle) draw the one under Mario: carried, or out in the room (whose
    # coordinates mean nothing in the level), he keeps the one he was taken from
    helpers = original("src/game/object_helpers.c")
    helpers = 'extern int unity_mario_carried(void);\n' + helpers
    helpers = replace_once(helpers, '        if (gMarioObject == NULL) {\n            switchCase->selectedCase = 0;\n        } else {',
                           '        if (unity_mario_carried()) {\n        } else if (gMarioObject == NULL) {\n'
                           '            switchCase->selectedCase = 0;\n        } else {')
    (root / "src/game/object_helpers.c").write_text(helpers)
    # In MR the window can be looked into from the side: the sky extends well beyond the screen (more tiles of
    # its panorama around the original 3x3, wrapping round, its top and bottom rows repeated)
    sky = original("src/game/skybox.c")
    sky = '#define UNITY_SKY_EXTRA 3\nextern int unity_is_mr(void);\n' + sky
    sky = replace_once(sky, """    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            s32 tileIndex = sSkyBoxInfo[player].upperLeftTile + row * SKYBOX_COLS + col;
            const u8 *const texture =
                (*(SkyboxTexture *) segmented_to_virtual(sSkyboxTextures[background]))[tileIndex];
            Vtx *vertices = make_skybox_rect(tileIndex, colorIndex);
""", """    s32 extra = unity_is_mr() ? UNITY_SKY_EXTRA : 0;
    s32 top = sSkyBoxInfo[player].upperLeftTile / SKYBOX_COLS, left = sSkyBoxInfo[player].upperLeftTile % SKYBOX_COLS;
    for (row = -extra; row < 3 + extra; row++) {
        for (col = -extra; col < 3 + extra; col++) {
            s32 tileRow = top + row, tileCol = left + col;
            s32 imageRow = tileRow < 0 ? 0 : tileRow >= SKYBOX_ROWS ? SKYBOX_ROWS - 1 : tileRow;
            s32 imageCol = ((tileCol % 8) + 8) % 8; /* the panorama is 8 tiles round (the last 2 repeat) */
            const u8 *const texture =
                (*(SkyboxTexture *) segmented_to_virtual(sSkyboxTextures[background]))[imageRow * SKYBOX_COLS + imageCol];
            Vtx *vertices = make_skybox_rect(0, colorIndex);
            for (s32 v = 0; vertices != NULL && v < 4; v++) {
                vertices[v].v.ob[0] += tileCol * SKYBOX_TILE_WIDTH;
                vertices[v].v.ob[1] -= tileRow * SKYBOX_TILE_HEIGHT;
            }
""")
    sky = replace_once(sky, '    s32 dlCommandCount = 5 + (3 * 3) * 7; // 5 for the start and end, plus 9 skybox tiles',
                       '    s32 dlCommandCount = 5 + (3 + 2 * UNITY_SKY_EXTRA) * (3 + 2 * UNITY_SKY_EXTRA) * 7;')
    # Billboards (trees, coins) face your head rather than the game camera, which you look past (sm64unity_billboards)
    maths = original("src/engine/math_util.c")
    maths = replace_once(maths, "void mtxf_billboard(Mat4 dest, Mat4 mtx, Vec3f position, s16 angle) {\n",
                         "extern int unity_billboard(Mat4 dest, Mat4 mtx, Vec3f position, s16 angle);\n"
                         "void mtxf_billboard(Mat4 dest, Mat4 mtx, Vec3f position, s16 angle) {\n"
                         "    if (unity_billboard(dest, mtx, position, angle)) return;\n")
    (root / "src/engine/math_util.c").write_text(maths)
    # Which sky it is, for Unity's dome of it round the game (sm64unity_sky)
    sky = replace_once(sky, "    return init_skybox_display_list(player, background, colorIndex);\n}",
                       "    unity_note_sky(background, colorIndex);\n    return init_skybox_display_list(player, background, colorIndex);\n}")
    sky = sky.replace('extern int unity_is_mr(void);\n', 'extern int unity_is_mr(void);\nextern void unity_note_sky(int background, int color);\n', 1)
    (root / "src/game/skybox.c").write_text(sky)
    surface = original("src/engine/surface_load.c") + '\n#include "pc/unity_room.inc.h"\n'
    (root / "src/engine/surface_load.c").write_text(surface)
    (root / "src/pc/unity_room.inc.c").unlink(missing_ok=True)
    # The PC port's options menu (R in the pause menu) holds Mario 64 MR's own options instead of its menus
    # (unity_options.inc.h); what it saved to a config file, Unity keeps
    options = original("src/game/options_menu.c")
    start = options.index("static struct Option optsControls[] = {")
    end_marker = "static struct SubMenu menuMain = DEF_SUBMENU( menuStr[0], optsMain );\n"
    end = options.index(end_marker) + len(end_marker)
    options = options[:start] + '#include "pc/unity_options.inc.h"\n' + options[end:]
    options = replace_once(options, "        if (!Cheats.EnableCheats) {", "        if (0) { // (no cheats menu)")
    options = replace_once(options, "        controller_reconfigure(); // rebind using new config values\n        configfile_save(configfile_name());\n", "")
    (root / "src/game/options_menu.c").write_text(options)
    strings = original("include/text_options_strings.h.in")
    strings = replace_once(strings, "#endif // TEXT_OPTIONS_STRINGS_H", MR_STRINGS + "\n#endif // TEXT_OPTIONS_STRINGS_H")
    (root / "include/text_options_strings.h.in").write_text(strings)
    for name in ("unity_bridge.h", "unity_bridge.c", "unity_mr.c", "unity_room.inc.h", "unity_options.h", "unity_options.inc.h"):
        shutil.copy2(HERE / name, root / "src/pc" / name)
    (root / "baserom.us.z64").write_bytes(rom)
    # Sequence zero is authored as data-only assembly upstream. Read its exact
    # original bytes from the supplied ROM instead of requiring a cross assembler.
    table = 0x7B0860
    offset, size = struct.unpack_from(">II", rom, table + 4)
    if size <= 0 or table + offset + size > len(rom):
        raise ValueError("Invalid ROM audio sequence table")
    (root / "sound/sequences/00_sound_player.m64").write_bytes(rom[table + offset:table + offset + size])
    (root / "sound/sequences/00_sound_player.s").unlink(missing_ok=True)

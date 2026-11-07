/* Mario 64 MR's options, in place of the PC port's own menus (src/game/options_menu.c includes this): the
 * headset's graphics, which Unity applies and keeps (sm64unity_options), the game's sound volumes and HUD, and
 * bringing Mario back from the room. Opened as the PC port's is: R in the pause menu. */

#include "pc/unity_options.h"

static const u8 mrMenuStr[][32] = {
    { TEXT_MR_GRAPHICS },
    { TEXT_OPT_AUDIO },
    { TEXT_OPT_HUD },
    { TEXT_MR_BRING_BACK },
    { TEXT_MR_REFRESH },
    { TEXT_MR_RESOLUTION },
    { TEXT_MR_ANTIALIASING },
    { TEXT_MR_FOVEATION },
    { TEXT_MR_TEXTURES },
    { TEXT_MR_SHARPENING },
    { TEXT_MR_DEFAULTS },
};

static const u8 mrRateStr[][8] = { { TEXT_MR_72HZ }, { TEXT_MR_80HZ }, { TEXT_MR_90HZ }, { TEXT_MR_120HZ } };
static const u8 mrScaleStr[][8] = {
    { TEXT_MR_70 }, { TEXT_MR_80 }, { TEXT_MR_90 }, { TEXT_MR_100 }, { TEXT_MR_110 },
    { TEXT_MR_120 }, { TEXT_MR_130 }, { TEXT_MR_140 }, { TEXT_MR_150 },
};
static const u8 mrAaStr[][16] = { { TEXT_MR_OFF }, { TEXT_MR_MSAA2 }, { TEXT_MR_MSAA4 } };
static const u8 mrFoveationStr[][16] = {
    { TEXT_MR_OFF }, { TEXT_MR_LOW }, { TEXT_MR_MEDIUM }, { TEXT_MR_HIGH }, { TEXT_MR_HIGHTOP },
};
static const u8 mrTextureStr[][24] = {
    { TEXT_MR_N64 }, { TEXT_MR_MIPMAPS }, { TEXT_MR_ANISO2 }, { TEXT_MR_ANISO4 }, { TEXT_MR_ANISO8 }, { TEXT_MR_ANISO16 },
};
static const u8 mrSharpenStr[][16] = { { TEXT_MR_OFF }, { TEXT_MR_NORMAL }, { TEXT_MR_QUALITY } };

static const u8 *mrRateChoices[] = { mrRateStr[0], mrRateStr[1], mrRateStr[2], mrRateStr[3] };
static const u8 *mrScaleChoices[] = {
    mrScaleStr[0], mrScaleStr[1], mrScaleStr[2], mrScaleStr[3], mrScaleStr[4],
    mrScaleStr[5], mrScaleStr[6], mrScaleStr[7], mrScaleStr[8],
};
static const u8 *mrAaChoices[] = { mrAaStr[0], mrAaStr[1], mrAaStr[2] };
static const u8 *mrFoveationChoices[] = {
    mrFoveationStr[0], mrFoveationStr[1], mrFoveationStr[2], mrFoveationStr[3], mrFoveationStr[4],
};
static const u8 *mrTextureChoices[] = {
    mrTextureStr[0], mrTextureStr[1], mrTextureStr[2], mrTextureStr[3], mrTextureStr[4], mrTextureStr[5],
};
static const u8 *mrSharpenChoices[] = { mrSharpenStr[0], mrSharpenStr[1], mrSharpenStr[2] };
static const u8 *mrToggleChoices[] = { toggleStr[0], toggleStr[1] };

/* (A pressed on them: arg 0; the stick sideways is +-1, and does nothing) */
static void mr_bring_back(UNUSED struct Option *self, s32 arg) {
    if (arg) return;
    gUnityOptions[UNITY_OPT_BRING_BACK]++;
    optmenu_toggle();
}

static void mr_defaults(UNUSED struct Option *self, s32 arg) {
    if (!arg) gUnityOptions[UNITY_OPT_DEFAULTS]++;
}

static struct Option optsGraphics[] = {
    DEF_OPT_CHOICE( mrMenuStr[4], &gUnityOptions[UNITY_OPT_REFRESH], mrRateChoices ),
    DEF_OPT_CHOICE( mrMenuStr[5], &gUnityOptions[UNITY_OPT_RESOLUTION], mrScaleChoices ),
    DEF_OPT_CHOICE( mrMenuStr[6], &gUnityOptions[UNITY_OPT_ANTIALIASING], mrAaChoices ),
    DEF_OPT_CHOICE( mrMenuStr[7], &gUnityOptions[UNITY_OPT_FOVEATION], mrFoveationChoices ),
    DEF_OPT_CHOICE( mrMenuStr[8], &gUnityOptions[UNITY_OPT_TEXTURES], mrTextureChoices ),
    DEF_OPT_CHOICE( mrMenuStr[9], &gUnityOptions[UNITY_OPT_SHARPENING], mrSharpenChoices ),
    DEF_OPT_BUTTON( mrMenuStr[10], mr_defaults ),
};

static struct Option optsAudio[] = {
    DEF_OPT_SCROLL( optsAudioStr[0], &gUnityOptions[UNITY_OPT_MASTER_VOLUME], 0, MAX_VOLUME, 1 ),
    DEF_OPT_SCROLL( optsAudioStr[1], &gUnityOptions[UNITY_OPT_MUSIC_VOLUME], 0, MAX_VOLUME, 1 ),
    DEF_OPT_SCROLL( optsAudioStr[2], &gUnityOptions[UNITY_OPT_SFX_VOLUME], 0, MAX_VOLUME, 1 ),
    DEF_OPT_SCROLL( optsAudioStr[3], &gUnityOptions[UNITY_OPT_ENV_VOLUME], 0, MAX_VOLUME, 1 ),
};

static struct SubMenu menuGraphics = DEF_SUBMENU( mrMenuStr[0], optsGraphics );
static struct SubMenu menuAudio    = DEF_SUBMENU( mrMenuStr[1], optsAudio );

static struct Option optsMain[] = {
    DEF_OPT_SUBMENU( mrMenuStr[0], &menuGraphics ),
    DEF_OPT_SUBMENU( mrMenuStr[1], &menuAudio ),
    DEF_OPT_CHOICE ( mrMenuStr[2], &gUnityOptions[UNITY_OPT_HUD], mrToggleChoices ),
    DEF_OPT_BUTTON ( mrMenuStr[3], mr_bring_back ),
};

static struct SubMenu menuMain = DEF_SUBMENU( menuStr[0], optsMain );

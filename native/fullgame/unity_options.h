#ifndef SM64_UNITY_OPTIONS_H
#define SM64_UNITY_OPTIONS_H
/* The options menu's values (unity_options.inc.h), shared with Unity (sm64unity_options, FullGame's GameOptions:
 * the same order). The graphics ones are choices Unity applies; the counters go up each time their button is
 * pressed. */
enum UnityOption {
    UNITY_OPT_REFRESH,       /* 72, 80, 90, 120 Hz */
    UNITY_OPT_RESOLUTION,    /* 70% to 150%, by 10 */
    UNITY_OPT_ANTIALIASING,  /* off, MSAA 2x, 4x */
    UNITY_OPT_FOVEATION,     /* off, low, medium, high, high top */
    UNITY_OPT_TEXTURES,      /* N64, mipmaps, anisotropic 2x, 4x, 8x, 16x */
    UNITY_OPT_SHARPENING,    /* off, normal, quality */
    UNITY_OPT_BRING_BACK,    /* counter: Mario back into the game */
    UNITY_OPT_DEFAULTS,      /* counter: the graphics back to their defaults */
    UNITY_OPT_MASTER_VOLUME, /* 0-127 */
    UNITY_OPT_MUSIC_VOLUME,
    UNITY_OPT_SFX_VOLUME,
    UNITY_OPT_ENV_VOLUME,
    UNITY_OPT_HUD,           /* 0 hidden, 1 shown */
    UNITY_OPT_COUNT
};
extern unsigned int gUnityOptions[UNITY_OPT_COUNT];
/* Each frame, before the game's: the volumes and the HUD into the PC port's settings */
void unity_apply_options(void);
#endif

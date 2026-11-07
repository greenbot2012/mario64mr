#ifndef SM64_UNITY_BRIDGE_H
#define SM64_UNITY_BRIDGE_H
#include <stdint.h>
#include "gfx/gfx_rendering_api.h"
#include "gfx/gfx_window_manager_api.h"
#include "audio/audio_api.h"
extern struct GfxRenderingAPI gfx_unity_api;
extern struct GfxWindowManagerAPI window_unity_api;
extern struct AudioAPI audio_unity_api;
void unity_read_controller(OSContPad *pad);
void unity_read_cursor(void);
#endif

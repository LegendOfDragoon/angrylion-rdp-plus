/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-video-angrylionplus - plugin.c                            *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define DP_INTERRUPT    0x20

#define M64P_PLUGIN_PROTOTYPES 1

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "api/m64p_types.h"
#include "api/m64p_config.h"
#include "screen_opengl_m64p.h"
#include "plugin.h"
#include "core/version.h"
#include "core/msg.h"
#include "core/rdram.h"

static ptr_ConfigOpenSection      ConfigOpenSection = NULL;
static ptr_ConfigSaveSection      ConfigSaveSection = NULL;
static ptr_ConfigSetDefaultInt    ConfigSetDefaultInt = NULL;
static ptr_ConfigSetDefaultBool   ConfigSetDefaultBool = NULL;
static ptr_ConfigGetParamInt      ConfigGetParamInt = NULL;
static ptr_ConfigGetParamBool     ConfigGetParamBool = NULL;

static bool warn_hle;
static bool plugin_initialized;
static void (*debug_callback)(void *, int, const char *);
static void *debug_call_context;
static struct core_config config;

m64p_dynlib_handle CoreLibHandle;
GFX_INFO gfx;
void (*render_callback)(int);

extern int32_t window_width;
extern int32_t window_height;
extern int32_t window_fullscreen;

static uint32_t rdram_size;
static uint8_t* rdram_hidden_bits;

static m64p_handle configVideoGeneral = NULL;
static m64p_handle configVideoAngrylionPlus = NULL;

#define PLUGIN_VERSION              0x020000
#define VIDEO_PLUGIN_API_VERSION	0x020200

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle _CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    if (plugin_initialized) {
        return M64ERR_ALREADY_INIT;
    }

    /* first thing is to set the callback function for debug info */
    debug_callback = DebugCallback;
    debug_call_context = Context;

    CoreLibHandle = _CoreLibHandle;

    ConfigOpenSection = (ptr_ConfigOpenSection)DLSYM(CoreLibHandle, "ConfigOpenSection");
    ConfigSaveSection = (ptr_ConfigSaveSection)DLSYM(CoreLibHandle, "ConfigSaveSection");
    ConfigSetDefaultInt = (ptr_ConfigSetDefaultInt)DLSYM(CoreLibHandle, "ConfigSetDefaultInt");
    ConfigSetDefaultBool = (ptr_ConfigSetDefaultBool)DLSYM(CoreLibHandle, "ConfigSetDefaultBool");
    ConfigGetParamInt = (ptr_ConfigGetParamInt)DLSYM(CoreLibHandle, "ConfigGetParamInt");
    ConfigGetParamBool = (ptr_ConfigGetParamBool)DLSYM(CoreLibHandle, "ConfigGetParamBool");

    ConfigOpenSection("Video-General", &configVideoGeneral);
    ConfigOpenSection("Video-AngrylionPlus", &configVideoAngrylionPlus);

    ConfigSetDefaultBool(configVideoGeneral, "Fullscreen", 0, "Use fullscreen mode if True, or windowed mode if False ");
    ConfigSetDefaultInt(configVideoGeneral, "ScreenWidth", 640, "Width of output window or fullscreen width");
    ConfigSetDefaultInt(configVideoGeneral, "ScreenHeight", 480, "Height of output window or fullscreen height");

    ConfigSetDefaultInt(configVideoAngrylionPlus, "NumWorkers", 0, "Rendering Workers (0=Use all logical processors)");
    ConfigSetDefaultInt(configVideoAngrylionPlus, "ViMode", 0, "VI Mode (0=Filtered, 1=Unfiltered, 2=Depth, 3=Coverage)");

    ConfigSaveSection("Video-General");
    ConfigSaveSection("Video-AngrylionPlus");

    plugin_initialized = true;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!plugin_initialized) {
        return M64ERR_NOT_INIT;
    }

    /* reset some local variable */
    debug_callback = NULL;
    debug_call_context = NULL;

    plugin_initialized = false;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL) {
        *PluginType = M64PLUGIN_GFX;
    }

    if (PluginVersion != NULL) {
        *PluginVersion = PLUGIN_VERSION;
    }

    if (APIVersion != NULL) {
        *APIVersion = VIDEO_PLUGIN_API_VERSION;
    }

    if (PluginNamePtr != NULL) {
        *PluginNamePtr = CORE_NAME;
    }

    if (Capabilities != NULL) {
        *Capabilities = 0;
    }

    return M64ERR_SUCCESS;
}

EXPORT int CALL InitiateGFX (GFX_INFO Gfx_Info)
{
    gfx = Gfx_Info;

    return 1;
}

EXPORT void CALL MoveScreen (int xpos, int ypos)
{
}

EXPORT void CALL ProcessDList(void)
{
    if (!warn_hle) {
        msg_warning("Please disable 'Graphic HLE' in the plugin settings.");
        warn_hle = true;
    }
}

EXPORT void CALL ProcessRDPList(void)
{
    core_update_dp();
}

EXPORT int CALL RomOpen (void)
{
    window_fullscreen = ConfigGetParamBool(configVideoGeneral, "Fullscreen");
    window_width = ConfigGetParamInt(configVideoGeneral, "ScreenWidth");
    window_height = ConfigGetParamInt(configVideoGeneral, "ScreenHeight");

    config.num_workers = ConfigGetParamInt(configVideoAngrylionPlus, "NumWorkers");
    config.vi_mode = ConfigGetParamInt(configVideoAngrylionPlus, "ViMode");

    core_init(&config, screen_opengl_m64p, plugin_mupen64plus);
    return 1;
}

EXPORT void CALL RomClosed (void)
{
    core_close();
}

EXPORT void CALL ShowCFB (void)
{
}

EXPORT void CALL UpdateScreen (void)
{
    core_update_vi();
}

EXPORT void CALL ViStatusChanged (void)
{
}

EXPORT void CALL ViWidthChanged (void)
{
}

EXPORT void CALL ChangeWindow(void)
{
    core_toggle_fullscreen();
}

EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int front)
{
    ogl_readscreen(dest, width, height, front);
}

EXPORT void CALL SetRenderingCallback(void (*callback)(int))
{
    render_callback = callback;
}

EXPORT void CALL ResizeVideoOutput(int width, int height)
{
    window_width = width;
    window_height = height;
}

EXPORT void CALL FBWrite(unsigned int addr, unsigned int size)
{
}

EXPORT void CALL FBRead(unsigned int addr)
{
}

EXPORT void CALL FBGetFrameBufferInfo(void *pinfo)
{
}

static char filter_char(char c)
{
    if (isalnum(c) || c == '_' || c == '-' || c == '.') {
        return c;
    } else {
        return ' ';
    }
}

static void plugin_init(void)
{
    rdram_size = 0x800000;

    // Zilmar plugins also can't access the hidden bits, so allocate it on our own
    rdram_hidden_bits = malloc(rdram_size);
    memset(rdram_hidden_bits, 3, rdram_size);
}

static void plugin_sync_dp(void)
{
    *gfx.MI_INTR_REG |= DP_INTERRUPT;
    gfx.CheckInterrupts();
}

static uint32_t** plugin_get_dp_registers(void)
{
    // HACK: this only works because the ordering of registers in GFX_INFO is
    // the same as in dp_register
    return (uint32_t**)&gfx.DPC_START_REG;
}

static uint32_t** plugin_get_vi_registers(void)
{
    // HACK: this only works because the ordering of registers in GFX_INFO is
    // the same as in vi_register
    return (uint32_t**)&gfx.VI_STATUS_REG;
}

static uint8_t* plugin_get_rdram(void)
{
    return gfx.RDRAM;
}

static uint8_t* plugin_get_rdram_hidden(void)
{
    return rdram_hidden_bits;
}

static uint32_t plugin_get_rdram_size(void)
{
    return rdram_size;
}

static uint8_t* plugin_get_dmem(void)
{
    return gfx.DMEM;
}

static uint32_t plugin_get_rom_name(char* name, uint32_t name_size)
{
    if (name_size < 21) {
        // buffer too small
        return 0;
    }

    // copy game name from ROM header, which is encoded in Shift_JIS.
    // most games just use the ASCII subset, so filter out the rest.
    int i = 0;
    for (; i < 20; i++) {
        name[i] = filter_char(gfx.HEADER[(32 + i) ^ BYTE_ADDR_XOR]);
    }

    // make sure there's at least one whitespace that will terminate the string
    // below
    name[i] = ' ';

    // trim trailing whitespaces
    for (; i > 0; i--) {
        if (name[i] != ' ') {
            break;
        }
        name[i] = 0;
    }

    // game title is empty or invalid, use safe fallback using the four-character
    // game ID
    if (i == 0) {
        for (; i < 4; i++) {
            name[i] = filter_char(gfx.HEADER[(59 + i) ^ BYTE_ADDR_XOR]);
        }
        name[i] = 0;
    }

    return i;
}

static void plugin_close(void)
{
    if (rdram_hidden_bits) {
        free(rdram_hidden_bits);
        rdram_hidden_bits = NULL;
    }
}

void plugin_mupen64plus(struct plugin_api* api)
{
    api->init = plugin_init;
    api->sync_dp = plugin_sync_dp;
    api->get_dp_registers = plugin_get_dp_registers;
    api->get_vi_registers = plugin_get_vi_registers;
    api->get_rdram = plugin_get_rdram;
    api->get_rdram_hidden = plugin_get_rdram_hidden;
    api->get_rdram_size = plugin_get_rdram_size;
    api->get_dmem = plugin_get_dmem;
    api->get_rom_name = plugin_get_rom_name;
    api->close = plugin_close;
}

/*
 * Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
 *
 * (c) Copyright 1996 - 2001 Gary Henderson (gary.henderson@ntlworld.com) and
 *                           Jerremy Koot (jkoot@snes9x.com)
 *
 * Super FX C emulator code
 * (c) Copyright 1997 - 1999 Ivar (ivar@snes9x.com) and
 *                           Gary Henderson.
 *
 * (c) Copyright 2014 - 2016 Daniel De Matteis. (UNDER NO CIRCUMSTANCE 
 * WILL COMMERCIAL RIGHTS EVER BE APPROPRIATED TO ANY PARTY)
 *
 * Super FX assembler emulator code (c) Copyright 1998 zsKnight and _Demo_.
 *
 * DSP1 emulator code (c) Copyright 1998 Ivar, _Demo_ and Gary Henderson.
 * C4 asm and some C emulation code (c) Copyright 2000 zsKnight and _Demo_.
 * C4 C code (c) Copyright 2001 Gary Henderson (gary.henderson@ntlworld.com).
 *
 * DOS port code contains the works of other authors. See headers in
 * individual files.
 *
 * Snes9x homepage: http://www.snes9x.com
 *
 * Permission to use, copy, modify and distribute Snes9x in both binary and
 * source form, for non-commercial purposes, is hereby granted without fee,
 * providing that this license information and copyright notice appear with
 * all copies and any derived work.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event shall the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Snes9x is freeware for PERSONAL USE only. Commercial users should
 * seek permission of the copyright holders first. Commercial use includes
 * charging money for Snes9x or software derived from Snes9x.
 *
 * The copyright holders request that bug fixes and improvements to the code
 * should be forwarded to them so everyone can benefit from the modifications
 * in future versions.
 *
 * Super NES and Super Nintendo Entertainment System are trademarks of
 * Nintendo Co., Limited and its subsidiary companies.
 */

#include <stdio.h>
#include <stdint.h>
#include <boolean.h>
#ifdef _MSC_VER
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <libretro.h>
#include <streams/memory_stream.h>

#include "../src/snes9x.h"
#include "../src/memmap.h"
#include "../src/cpuexec.h"
#include "../src/srtc.h"
#include "../src/apu.h"
#include "../src/ppu.h"
#include "../src/snapshot.h"
#include "../src/soundux.h"
#include "../src/cheats.h"
#include "../src/display.h"
#include "../src/os9x_asm_cpu.h"
#include "../src/controls.h"

#ifdef _3DS
void* linearMemAlign(size_t size, size_t alignment);
void linearFree(void* mem);
#endif

#define RETRO_DEVICE_JOYPAD_MULTITAP ((1 << 8) | RETRO_DEVICE_JOYPAD)
#define RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE ((1 << 8) | RETRO_DEVICE_LIGHTGUN)
#define RETRO_DEVICE_LIGHTGUN_JUSTIFIER ((2 << 8) | RETRO_DEVICE_LIGHTGUN)
#define RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2 ((3 << 8) | RETRO_DEVICE_LIGHTGUN)
#define RETRO_DEVICE_LIGHTGUN_MACS_RIFLE ((4 << 8) | RETRO_DEVICE_LIGHTGUN)

static int g_screen_gun_width = SNES_WIDTH;
static int g_screen_gun_height = SNES_HEIGHT;

#define MAP_BUTTON(id, name) S9xMapButton((id), S9xGetCommandT((name)), false)
#define MAKE_BUTTON(pad, btn) (((pad)<<4)|(btn))

#define PAD_1 1
#define PAD_2 2
#define PAD_3 3
#define PAD_4 4
#define PAD_5 5

#define BTN_B RETRO_DEVICE_ID_JOYPAD_B
#define BTN_Y RETRO_DEVICE_ID_JOYPAD_Y
#define BTN_SELECT RETRO_DEVICE_ID_JOYPAD_SELECT
#define BTN_START RETRO_DEVICE_ID_JOYPAD_START
#define BTN_UP RETRO_DEVICE_ID_JOYPAD_UP
#define BTN_DOWN RETRO_DEVICE_ID_JOYPAD_DOWN
#define BTN_LEFT RETRO_DEVICE_ID_JOYPAD_LEFT
#define BTN_RIGHT RETRO_DEVICE_ID_JOYPAD_RIGHT
#define BTN_A RETRO_DEVICE_ID_JOYPAD_A
#define BTN_X RETRO_DEVICE_ID_JOYPAD_X
#define BTN_L RETRO_DEVICE_ID_JOYPAD_L
#define BTN_R RETRO_DEVICE_ID_JOYPAD_R
#define BTN_FIRST BTN_B
#define BTN_LAST BTN_R

#define MOUSE_X RETRO_DEVICE_ID_MOUSE_X
#define MOUSE_Y RETRO_DEVICE_ID_MOUSE_Y
#define MOUSE_LEFT RETRO_DEVICE_ID_MOUSE_LEFT
#define MOUSE_RIGHT RETRO_DEVICE_ID_MOUSE_RIGHT
#define MOUSE_FIRST MOUSE_X
#define MOUSE_LAST MOUSE_RIGHT

static int scope_buttons[] =
{
  RETRO_DEVICE_ID_LIGHTGUN_TRIGGER, // 2
  RETRO_DEVICE_ID_LIGHTGUN_CURSOR, // 3
  RETRO_DEVICE_ID_LIGHTGUN_TURBO, // 4
  RETRO_DEVICE_ID_LIGHTGUN_START, // 5
  RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN, // 6
};
static int scope_button_count = sizeof( scope_buttons ) / sizeof( int );

#define JUSTIFIER_TRIGGER 2
#define JUSTIFIER_START 3
#define JUSTIFIER_OFFSCREEN 4

#define MACS_RIFLE_TRIGGER 2

#define BTN_POINTER (RETRO_DEVICE_ID_JOYPAD_R + 1)
#define BTN_POINTER2 (BTN_POINTER + 1)

static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_environment_t environ_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;
static uint32 joys[5];

bool8 ROMAPUEnabled = 0;
char currentWorkingDir[MAX_PATH+1] = {0};
bool overclock_cycles = false;
int one_c, slow_one_c, two_c;

memstream_t *s_stream;

int s_open(const char *fname, const char *mode)
{
   s_stream = memstream_open(0);
   return TRUE;
}

int s_read(void *p, int l)
{
   return memstream_read(s_stream, p, l);
}

int s_write(void *p, int l)
{
   return memstream_write(s_stream, p, l);
}

void s_close(void)
{
   memstream_close(s_stream);
}

int  (*statef_open)(const char *fname, const char *mode) = s_open;
int  (*statef_read)(void *p, int l) = s_read;
int  (*statef_write)(void *p, int l) = s_write;
void (*statef_close)(void) = s_close;



void *retro_get_memory_data(unsigned type)
{
   uint8_t* data;

   switch(type)
   {
      case RETRO_MEMORY_SAVE_RAM:
         data = Memory.SRAM;
         break;
      case RETRO_MEMORY_SYSTEM_RAM:
         data = Memory.RAM;
         break;
      case RETRO_MEMORY_VIDEO_RAM:
         data = Memory.VRAM;
         break;
      default:
         data = NULL;
         break;
   }

   return data;
}

size_t retro_get_memory_size(unsigned type)
{
   unsigned size;

   switch(type)
   {
      case RETRO_MEMORY_SAVE_RAM:
         size = (unsigned) (Memory.SRAMSize ? (1 << (Memory.SRAMSize + 3)) * 128 : 0);
         if (size > 0x20000)
            size = 0x20000;
         break;
      /*case RETRO_MEMORY_RTC:
         size = (Settings.SRTC || Settings.SPC7110RTC)?20:0;
         break;*/
      case RETRO_MEMORY_SYSTEM_RAM:
         size = 128 * 1024;
         break;
      case RETRO_MEMORY_VIDEO_RAM:
         size = 64 * 1024;
         break;
      default:
         size = 0;
         break;
   }

   return size;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

static bool use_overscan;

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   static const struct retro_controller_description port_1[] = {
        { "None", RETRO_DEVICE_NONE },
        { "SNES Joypad", RETRO_DEVICE_JOYPAD },
        { "SNES Mouse", RETRO_DEVICE_MOUSE },
        { "Multitap", RETRO_DEVICE_JOYPAD_MULTITAP },
    };

    static const struct retro_controller_description port_2[] = {
        { "None", RETRO_DEVICE_NONE },
        { "SNES Joypad", RETRO_DEVICE_JOYPAD },
        { "SNES Mouse", RETRO_DEVICE_MOUSE },
        { "Multitap", RETRO_DEVICE_JOYPAD_MULTITAP },
        { "SuperScope", RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE },
        { "Justifier", RETRO_DEVICE_LIGHTGUN_JUSTIFIER },
        { "M.A.C.S. Rifle", RETRO_DEVICE_LIGHTGUN_MACS_RIFLE },
    };

    static const struct retro_controller_description port_3[] = {
        { "None", RETRO_DEVICE_NONE },
        { "SNES Joypad", RETRO_DEVICE_JOYPAD },
        { "Justifier (2P)", RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2 },
    };

    static const struct retro_controller_description port_extra[] = {
        { "None", RETRO_DEVICE_NONE },
        { "SNES Joypad", RETRO_DEVICE_JOYPAD },
    };

    static const struct retro_controller_info ports[] = {
        { port_1, 4 },
        { port_2, 7 },
        { port_3, 3 },
        { port_extra, 2 },
        { port_extra, 2 },
        { port_extra, 2 },
        { port_extra, 2 },
        { port_extra, 2 },
        {},
    };

    environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

void retro_get_system_info(struct retro_system_info *info)
{
   info->need_fullpath =   false;
   info->valid_extensions = "smc|fig|sfc|gd3|gd7|dx2|bsx|swc";
   info->library_version  = "7.2.0";
   info->library_name     = "Snes9x 2002";
   info->block_extract    = false;
}

static int16 audio_buf[0x10000];
static unsigned avail;
static float samplerate = 32040.5f;

void S9xGenerateSound(void)
{
}

uint32 S9xReadJoypad(int which1)
{
   if (which1 > 4)
      return 0;
   return joys[which1];
}

static unsigned snes_devices[8];
void retro_set_controller_port_device(unsigned port, unsigned device)
{
    if (port < 8)
    {
        int offset = snes_devices[0] == RETRO_DEVICE_JOYPAD_MULTITAP ? 4 : 1;
        switch (device)
        {
            case RETRO_DEVICE_JOYPAD:
                S9xSetController(port, CTL_JOYPAD, port * offset, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_JOYPAD;
                break;
            case RETRO_DEVICE_JOYPAD_MULTITAP:
                S9xSetController(port, CTL_MP5, port * offset, port * offset + 1, port * offset + 2, port * offset + 3);
                snes_devices[port] = RETRO_DEVICE_JOYPAD_MULTITAP;
                break;
            case RETRO_DEVICE_MOUSE:
                S9xSetController(port, CTL_MOUSE, port, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_MOUSE;
                break;
            case RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE:
                S9xSetController(port, CTL_SUPERSCOPE, 0, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE;
                break;
            case RETRO_DEVICE_LIGHTGUN_JUSTIFIER:
                S9xSetController(port, CTL_JUSTIFIER, 0, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_LIGHTGUN_JUSTIFIER;
                break;
            case RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2:
              if ( port == 2 )
              {
          S9xSetController(1, CTL_JUSTIFIER, 1, 0, 0, 0);
                  snes_devices[port] = RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2;
        }
        else
        {
          printf("Invalid Justifier (2P) assignment to port %d, must be port 2.\n", port);
          S9xSetController(port, CTL_NONE, 0, 0, 0, 0);
          snes_devices[port] = RETRO_DEVICE_NONE;
        }
                break;
            case RETRO_DEVICE_LIGHTGUN_MACS_RIFLE:
                S9xSetController(port, CTL_MACSRIFLE, 0, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_LIGHTGUN_MACS_RIFLE;
                break;
            case RETRO_DEVICE_NONE:
                S9xSetController(port, CTL_NONE, 0, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_NONE;
                break;
            default:
                printf("Invalid device (%d).\n", device);
                break;
        }
        
        S9xControlsSoftReset();
    }
    else if(device != RETRO_DEVICE_NONE)
        printf("Nonexistent Port (%d).\n", port);
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width = SNES_WIDTH;
   info->geometry.base_height = SNES_HEIGHT;
   info->geometry.max_width = 512;
   info->geometry.max_height = 512;

   if(PPU.ScreenHeight == SNES_HEIGHT_EXTENDED)
      info->geometry.base_height = SNES_HEIGHT_EXTENDED;

   if (!Settings.PAL)
      info->timing.fps = 21477272.0 / 357366.0;
   else
      info->timing.fps = 21281370.0 / 425568.0;

   info->timing.sample_rate = samplerate;
   info->geometry.aspect_ratio = 4.0f / 3.0f;
}

static void map_buttons(void);

static void snes_init (void)
{
   const int safety = 128;

   memset(&Settings, 0, sizeof(Settings));
	Settings.JoystickEnabled = FALSE;
	Settings.SoundPlaybackRate = samplerate;
	Settings.Stereo = TRUE;
	Settings.SoundBufferSize = 0;
	Settings.CyclesPercentage = 100;
	Settings.DisableSoundEcho = FALSE;
	Settings.APUEnabled = FALSE;
	Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
	Settings.SkipFrames = AUTO_FRAMERATE;
	Settings.Shutdown = Settings.ShutdownMaster = TRUE;
	Settings.FrameTimePAL = 20000;
	Settings.FrameTimeNTSC = 16667;
	Settings.FrameTime = Settings.FrameTimeNTSC;
	Settings.DisableSampleCaching = FALSE;
	Settings.DisableMasterVolume = FALSE;
	Settings.Mouse = FALSE;
	Settings.SuperScope = FALSE;
	Settings.MultiPlayer5Master = TRUE;
	Settings.ControllerOption = SNES_MULTIPLAYER5;
	Settings.ControllerOption = 0;
	
	Settings.ForceTransparency = FALSE;
	Settings.Transparency = TRUE;
	Settings.SixteenBit = TRUE;
	
	Settings.SupportHiRes = FALSE;
	Settings.AutoSaveDelay = 30;
	Settings.ApplyCheats = TRUE;
	Settings.TurboMode = FALSE;
	Settings.TurboSkipFrames = 15;
	Settings.SoundSync = FALSE;
#ifdef ASM_SPC700
	Settings.asmspc700 = TRUE;
#endif
	Settings.SpeedHacks = TRUE;

	Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;

   Settings.InterpolatedSound = TRUE;

   CPU.Flags = 0;

   if (!MemoryInit() || !S9xInitAPU())
   {
      MemoryDeinit();
      S9xDeinitAPU();
      fprintf(stderr, "[libsnes]: Failed to init Memory or APU.\n");
      exit(1);
   }

   if (!S9xInitSound() || !S9xGraphicsInit()) exit(1);
   //S9xSetSamplesAvailableCallback(S9xAudioCallback);

   GFX.Pitch = use_overscan ? 1024 : 2048;
   
   // hack to make sure GFX.Delta is always  (2048 * 512 * 2) >> 1, needed for tile16_t.h
#ifdef _3DS
   GFX.Screen_buffer = (uint8 *) linearMemAlign(2048 * 512 * 2 * 2 + safety, 0x80);
#else
   GFX.Screen_buffer = (uint8 *) calloc(1, 2048 * 512 * 2 * 2 + safety);
#endif
   GFX.Screen = GFX.Screen_buffer + safety;

   GFX.SubScreen = GFX.Screen + 2048 * 512 * 2;
   GFX.ZBuffer_buffer = (uint8 *) calloc(1, GFX.Pitch * 512 * sizeof(uint16) + safety);
   GFX.ZBuffer = GFX.ZBuffer_buffer + safety;
   GFX.SubZBuffer_buffer = (uint8 *) calloc(1, GFX.Pitch * 512 * sizeof(uint16) + safety);
   GFX.SubZBuffer = GFX.SubZBuffer_buffer + safety;
   GFX.Delta = 1048576; //(GFX.SubScreen - GFX.Screen) >> 1;

   if (GFX.Delta != ((GFX.SubScreen - GFX.Screen) >> 1))
   {
      printf("BAD DELTA! (is %u, should be %u)\n", ((GFX.SubScreen - GFX.Screen) >> 1), GFX.Delta);
      exit(1);
   }

    for (int i = 0; i < 2; i++)
    {
        S9xSetController(i, CTL_JOYPAD, i, 0, 0, 0);
        snes_devices[i] = RETRO_DEVICE_JOYPAD;
    }

    S9xUnmapAllControls();
    map_buttons();
}

void retro_init (void)
{
   static const struct retro_variable vars[] =
   {
      { "snes9x2002_overclock_cycles", "Reduce Slowdown (Hack, Unsafe, Restart); disabled|compatible|max" },
      { NULL, NULL },
   };

   if (!environ_cb(RETRO_ENVIRONMENT_GET_OVERSCAN, &use_overscan))
	   use_overscan = FALSE;

   snes_init();
   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

/* libsnes uses relative values for analogue devices. 
   S9x seems to use absolute values, but do convert these into relative values in the core. (Why?!)
   Hack around it. :) */

void retro_deinit(void)
{
   S9xDeinitAPU();
   MemoryDeinit();
   S9xGraphicsDeinit();
   //S9xUnmapAllControls();
   if(GFX.Screen_buffer)
#ifdef _3DS
      linearFree(GFX.Screen_buffer);
#else
      free(GFX.Screen_buffer);
#endif
   GFX.Screen_buffer = NULL;
   GFX.Screen = NULL;
   GFX.SubScreen = NULL;

   if(GFX.ZBuffer_buffer)
      free(GFX.ZBuffer_buffer);
   GFX.ZBuffer_buffer = NULL;

   if(GFX.SubZBuffer_buffer)
      free(GFX.SubZBuffer_buffer);

   GFX.SubZBuffer_buffer = NULL;
}


static void map_buttons()
{
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_A), "Joypad1 A");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_B), "Joypad1 B");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_X), "Joypad1 X");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_Y), "Joypad1 Y");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_SELECT), "{Joypad1 Select,Mouse1 L}");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_START), "{Joypad1 Start,Mouse1 R}");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_L), "Joypad1 L");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_R), "Joypad1 R");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_LEFT), "Joypad1 Left");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_RIGHT), "Joypad1 Right");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_UP), "Joypad1 Up");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, BTN_DOWN), "Joypad1 Down");
    S9xMapPointer((BTN_POINTER), S9xGetCommandT("Pointer Mouse1+Superscope+Justifier1+MacsRifle"), false);
    S9xMapPointer((BTN_POINTER2), S9xGetCommandT("Pointer Mouse2+Justifier2"), false);

    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_B), "Joypad2 B");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_Y), "Joypad2 Y");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_SELECT), "{Joypad2 Select,Mouse2 L,Superscope Fire,Justifier1 Trigger,MacsRifle Trigger}");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_START), "{Joypad2 Start,Mouse2 R,Superscope Cursor,Justifier1 Start}");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_UP), "{Joypad2 Up,Superscope ToggleTurbo,Justifier1 AimOffscreen}");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_DOWN), "{Joypad2 Down,Superscope Pause}");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_LEFT), "{Joypad2 Left,Superscope AimOffscreen}");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_RIGHT), "Joypad2 Right");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_A), "Joypad2 A");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_X), "Joypad2 X");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_L), "Joypad2 L");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, BTN_R), "Joypad2 R");

    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_B), "Joypad3 B");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_Y), "Joypad3 Y");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_SELECT), "{Joypad3 Select,Justifier2 Trigger}");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_START), "{Joypad3 Start,Justifier2 Start}");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_UP), "{Joypad3 Up,Justifier2 AimOffscreen}");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_DOWN), "Joypad3 Down");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_LEFT), "Joypad3 Left");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_RIGHT), "Joypad3 Right");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_A), "Joypad3 A");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_X), "Joypad3 X");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_L), "Joypad3 L");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, BTN_R), "Joypad3 R");

    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_A), "Joypad4 A");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_B), "Joypad4 B");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_X), "Joypad4 X");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_Y), "Joypad4 Y");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_SELECT), "Joypad4 Select");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_START), "Joypad4 Start");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_L), "Joypad4 L");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_R), "Joypad4 R");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_LEFT), "Joypad4 Left");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_RIGHT), "Joypad4 Right");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_UP), "Joypad4 Up");
    MAP_BUTTON(MAKE_BUTTON(PAD_4, BTN_DOWN), "Joypad4 Down");

    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_A), "Joypad5 A");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_B), "Joypad5 B");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_X), "Joypad5 X");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_Y), "Joypad5 Y");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_SELECT), "Joypad5 Select");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_START), "Joypad5 Start");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_L), "Joypad5 L");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_R), "Joypad5 R");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_LEFT), "Joypad5 Left");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_RIGHT), "Joypad5 Right");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_UP), "Joypad5 Up");
    MAP_BUTTON(MAKE_BUTTON(PAD_5, BTN_DOWN), "Joypad5 Down");

}

static int16_t snes_mouse_state[2][2] = {{0}, {0}};
static bool snes_superscope_turbo_latch = false;

static void input_report_gun_position( unsigned port, int s9xinput )
{
  int x, y;

  x = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
  y = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);

  /*scale & clamp*/
  x = ( ( x + 0x7FFF ) * g_screen_gun_width ) / 0xFFFF;
  if ( x < 0 )
    x = 0;
  else if ( x >= g_screen_gun_width )
    x = g_screen_gun_width - 1;

  /*scale & clamp*/
  y = ( ( y + 0x7FFF ) * g_screen_gun_height ) / 0xFFFF;
  if ( y < 0 )
    y = 0;
  else if ( y >= g_screen_gun_height )
    y = g_screen_gun_height - 1;

  S9xReportPointer(s9xinput, (int16_t)x, (int16_t)y);
}

static void report_buttons()
{
    int offset = snes_devices[0] == RETRO_DEVICE_JOYPAD_MULTITAP ? 4 : 1;
    int _x, _y;

    for (int port = 0; port <= 1; port++)
    {
        switch (snes_devices[port])
        {
            case RETRO_DEVICE_JOYPAD:
                for (int i = BTN_FIRST; i <= BTN_LAST; i++)
                    S9xReportButton(MAKE_BUTTON(port * offset + 1, i), input_state_cb(port * offset, RETRO_DEVICE_JOYPAD, 0, i));
                break;

            case RETRO_DEVICE_JOYPAD_MULTITAP:
                for (int j = 0; j < 4; j++)
                    for (int i = BTN_FIRST; i <= BTN_LAST; i++)
                        S9xReportButton(MAKE_BUTTON(port * offset + j + 1, i), input_state_cb(port * offset + j, RETRO_DEVICE_JOYPAD, 0, i));
                break;

            case RETRO_DEVICE_MOUSE:
                _x = input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
                _y = input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
                snes_mouse_state[port][0] += _x;
                snes_mouse_state[port][1] += _y;
                S9xReportPointer(BTN_POINTER + port, snes_mouse_state[port][0], snes_mouse_state[port][1]);
                for (int i = MOUSE_LEFT; i <= MOUSE_LAST; i++)
                    S9xReportButton(MAKE_BUTTON(port + 1, i), input_state_cb(port, RETRO_DEVICE_MOUSE, 0, i));
                break;

            case RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE:

        input_report_gun_position( port, BTN_POINTER );

        for (int i = 0; i < scope_button_count; i++)
        {
          int id = scope_buttons[i];
          bool btn = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, id )?true:false;

          /* RETRO_DEVICE_ID_LIGHTGUN_TURBO special case - core needs a rising-edge trigger */
          if ( id == RETRO_DEVICE_ID_LIGHTGUN_TURBO )
          {
            bool old = btn;
            btn = btn && !snes_superscope_turbo_latch;
            snes_superscope_turbo_latch = old;
          }

          S9xReportButton(MAKE_BUTTON(PAD_2, i+2), btn);
        }
                break;

            case RETRO_DEVICE_LIGHTGUN_JUSTIFIER:

        input_report_gun_position( port, BTN_POINTER );

        {
          /* Special Reload Button */
          int btn_offscreen_shot = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD );

          /* Trigger ? */
          int btn_trigger = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER );
          S9xReportButton(MAKE_BUTTON(PAD_2, JUSTIFIER_TRIGGER), btn_trigger || btn_offscreen_shot);

          /* Start Button ? */
          int btn_start = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START );
          S9xReportButton(MAKE_BUTTON(PAD_2, JUSTIFIER_START), btn_start ? 1 : 0 );

          /* Aiming off-screen ? */
          int btn_offscreen = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN );
          S9xReportButton(MAKE_BUTTON(PAD_2, JUSTIFIER_OFFSCREEN), btn_offscreen || btn_offscreen_shot);
        }

        /* Second Gun? */
        if ( snes_devices[port+1] == RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2 )
        {
          int second = port+1;

          input_report_gun_position( second, BTN_POINTER2 );

          /* Special Reload Button */
          int btn_offscreen_shot = input_state_cb( second, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD );

          /* Trigger ? */
          int btn_trigger = input_state_cb( second, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER );
          S9xReportButton(MAKE_BUTTON(PAD_3, JUSTIFIER_TRIGGER), btn_trigger || btn_offscreen_shot);

          /* Start Button ? */
          int btn_start = input_state_cb( second, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START );
          S9xReportButton(MAKE_BUTTON(PAD_3, JUSTIFIER_START), btn_start ? 1 : 0 );

          /* Aiming off-screen ? */
          int btn_offscreen = input_state_cb( second, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN );
          S9xReportButton(MAKE_BUTTON(PAD_3, JUSTIFIER_OFFSCREEN), btn_offscreen || btn_offscreen_shot);
        }

                break;

            case RETRO_DEVICE_LIGHTGUN_MACS_RIFLE:

        input_report_gun_position( port, BTN_POINTER );

        {
          /* Trigger ? */
          int btn_trigger = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER );
          S9xReportButton(MAKE_BUTTON(PAD_2, MACS_RIFLE_TRIGGER), btn_trigger);
        }

                break;

            case RETRO_DEVICE_NONE:
                break;

            default:
                printf( "Unknown device...\n");
        }
    }
}


void retro_reset (void)
{
   S9xReset();
}

//static int16_t retro_mouse_state[2][2] = {{0}, {0}};
//static int16_t retro_scope_state[2] = {0};
//static int16_t retro_justifier_state[2][2] = {{0}, {0}};
void S9xSetButton(int i, uint16 b, bool pressed);

static void check_variables(void)
{
   struct retro_variable var;

   var.key = "snes9x2002_overclock_cycles";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
        if (strcmp(var.value, "compatible") == 0)
        {
           overclock_cycles = true;
           one_c = 4;
           slow_one_c = 5;
           two_c = 6;
        }
        else if (strcmp(var.value, "max") == 0)
        {
           overclock_cycles = true;
           one_c = 3;
           slow_one_c = 3;
           two_c = 3;
        }
        else
          overclock_cycles = false;
      }
}

//#define FRAME_SKIP

void retro_run (void)
{
   bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

#ifdef FRAME_SKIP
   IPPU.RenderThisFrame = !IPPU.RenderThisFrame;
#else
   IPPU.RenderThisFrame = TRUE;
#endif

   S9xMainLoop();
//   asm_S9xMainLoop();
   S9xMixSamples(audio_buf, avail);
   audio_batch_cb((int16_t *) audio_buf, avail >> 1);

#ifdef FRAME_SKIP
   if(!IPPU.RenderThisFrame)
      video_cb(NULL, IPPU.RenderedScreenWidth, IPPU.RenderedScreenHeight, GFX_PITCH);
#endif

   poll_cb();

   report_buttons();
}

size_t retro_serialize_size (void)
{
   uint8_t *tmpbuf;

   tmpbuf = (uint8_t*)malloc(5000000);
   memstream_set_buffer(tmpbuf, 5000000);
   S9xFreezeGame("");
   free(tmpbuf);
   return memstream_get_last_size();
}

bool retro_serialize(void *data, size_t size)
{
   memstream_set_buffer((uint8_t*)data, size);
   if (S9xFreezeGame("") == FALSE)
      return FALSE;

   return TRUE;
}

bool retro_unserialize(const void * data, size_t size)
{
   memstream_set_buffer((uint8_t*)data, size);
   if (S9xUnfreezeGame("") == FALSE)
      return FALSE;

   return TRUE;
}

void retro_cheat_reset(void)
{
    S9xDeleteCheats();
}

void retro_cheat_set(unsigned index, bool enable, const char* in_code)
{
    uint32 address;
    uint8 byte;
    // clean input
    char clean_code[strlen(in_code)];
    int j =0;
    unsigned i;

    for (i = 0; i < strlen(in_code); i++)          
    {
        switch (in_code[i])
        {
            case 'a': case 'A':
            case 'b': case 'B':
            case 'c': case 'C':
            case 'd': case 'D':
            case 'e': case 'E':
            case 'f': case 'F':
            
            case '-': case '0':
            case '1': case '2': case '3':
            case '4': case '5': case '6': 
            case '7': case '8': case '9':
                clean_code[j++]=in_code[i];
                break;
            default:
                break;
        }
    }
    clean_code[j]=0;
    
    if ( S9xProActionReplayToRaw(clean_code, &address, &byte) == NULL)
        S9xAddCheat(true, true, address, byte);
    else if ( S9xGameGenieToRaw(clean_code, &address, &byte) == NULL)
        S9xAddCheat(true, true, address, byte);
    /* else, silently ignore */
}

static void init_descriptors(void)
{
    struct retro_input_descriptor desc[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,   "D-Pad Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,    "B" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,    "A" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,    "X" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,    "Y" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,    "L" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,    "R" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,   "D-Pad Up" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,    "B" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,    "A" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,    "X" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,    "Y" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,    "L" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,    "R" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,   "D-Pad Up" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,    "B" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,    "A" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,    "X" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,    "Y" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,    "L" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,    "R" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,   "D-Pad Up" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,    "B" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,    "A" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,    "X" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,    "Y" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,    "L" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,    "R" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,   "D-Pad Up" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,    "B" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,    "A" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,    "X" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,    "Y" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,    "L" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,    "R" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

        { 0, 0, 0, 0, NULL },
    };

    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

bool retro_load_game(const struct retro_game_info *game)
{
   init_descriptors();

   bool8 loaded;
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   check_variables();

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "[libretro]: RGB565 is not supported.\n");
      return false;
   }

   /* Hack. S9x cannot do stuff from RAM. <_< */
   memstream_set_buffer((uint8_t*)game->data, game->size);

   loaded = LoadROM("");
   if (!loaded)
   {
      fprintf(stderr, "[libretro]: Rom loading failed...\n");
      return false;
   }

   //S9xGraphicsInit();
   S9xReset();
   CPU.APU_APUExecuting = Settings.APUEnabled = 1;
   Settings.SixteenBitSound = true;
   so.stereo = Settings.Stereo;
   so.playback_rate = Settings.SoundPlaybackRate;
   S9xSetPlaybackRate(so.playback_rate);
   S9xSetSoundMute(FALSE);

   avail = (int) (samplerate / (Settings.PAL ? 50 : 60)) << 1;

   memset(audio_buf, 0, sizeof(audio_buf));

   return true;
}

bool retro_load_game_special(
  unsigned game_type,
  const struct retro_game_info *info, size_t num_info
)
{ return false; }

void retro_unload_game (void)
{ }

unsigned retro_get_region (void)
{ 
   return Settings.PAL ? RETRO_REGION_PAL : RETRO_REGION_NTSC; 
}

bool8 S9xDeinitUpdate(int width, int height, bool8 sixteen_bit)
{
	int y;

	if (height == 448 || height == 478)
	{
		/* Pitch 2048 -> 1024, only done once per res-change. */
		if (GFX.Pitch == 2048)
		{
			for ( y = 1; y < height; y++)
			{
				uint8_t *src = GFX.Screen + y * 1024;
				uint8_t *dst = GFX.Screen + y * 512;
				memcpy(dst, src, width * sizeof(uint8_t) * 2);
			}
		}
		GFX.Pitch = 1024;
	}
	else
	{
		/* Pitch 1024 -> 2048, only done once per res-change. */
		if (GFX.Pitch == 1024)
		{
			for ( y = height - 1; y >= 0; y--)
			{
				uint8_t *src = GFX.Screen + y * 512;
				uint8_t *dst = GFX.Screen + y * 1024;
				memcpy(dst, src, width * sizeof(uint8_t) * 2);
			}
		}
		GFX.Pitch = 2048;
	}

	video_cb(GFX.Screen, width, height, GFX_PITCH);
	
	return TRUE;
}


/* Dummy functions that should probably be implemented correctly later. */
const char* S9xGetFilename(const char* in) { return in; }
const char* S9xGetFilenameInc(const char* in) { return in; }
const char *S9xGetHomeDirectory() { return NULL; }
const char *S9xGetSnapshotDirectory() { return NULL; }
const char *S9xGetROMDirectory() { return NULL; }
bool8 S9xInitUpdate() { return TRUE; }
bool8 S9xContinueUpdate(int width, int height) { return TRUE; }
void S9xSetPalette() {}
void S9xLoadSDD1Data() {}
bool8 S9xReadMousePosition (int which1_0_to_1, int* x, int* y, uint32* buttons) { return FALSE; }
bool8 S9xReadSuperScopePosition (int* x, int* y, uint32* buttons) { return FALSE; }
bool JustifierOffscreen() { return false; }

void S9xToggleSoundChannel (int channel) {}

const char *S9xStringInput(const char *message) { return NULL; }

//void Write16(uint16 v, uint8*& ptr) {}
//uint16 Read16(const uint8*& ptr) { return 0; }
const char* S9xChooseFilename(unsigned char name) { return ""; }
void S9xHandlePortCommand(s9xcommand_t cmd, int16 data1, int16 data2) {}
bool S9xPollButton(uint32 id, bool *pressed) { return false; }
bool S9xPollPointer(uint32 id, int16 *x, int16 *y) { return false; }
bool S9xPollAxis(uint32 id, int16 *value) { return false; }

void S9xExit() { exit(1); }
bool8 S9xOpenSoundDevice (int mode, bool8 stereo, int buffer_size) {
	//so.sixteen_bit = 1;
	so.stereo = TRUE;
	//so.buffer_size = 534;
	so.playback_rate = samplerate;
	return TRUE;
}

const char *emptyString = "";
const char *S9xBasename (const char *filename) { return emptyString; }

void S9xMessage(int a, int b, const char* msg)
{
   fprintf(stderr, "%s\n", msg);
}

/* S9x weirdness. */
#ifndef _WIN32
void _splitpath (const char * path, char * drive, char * dir, char * fname, char * ext)
{
	const char *slash, *dot;

	slash = strrchr(path, SLASH_CHAR);
	dot   = strrchr(path, '.');

	if (dot && slash && dot < slash)
		dot = NULL;

	if (!slash)
	{
		*dir = 0;

		strcpy(fname, path);

		if (dot)
		{
			fname[dot - path] = 0;
			strcpy(ext, dot + 1);
		}
		else
			*ext = 0;
	}
	else
	{
		strcpy(dir, path);
		dir[slash - path] = 0;

		strcpy(fname, slash + 1);

		if (dot)
		{
			fname[dot - slash - 1] = 0;
			strcpy(ext, dot + 1);
		}
		else
			*ext = 0;
	}
}

void _makepath (char *path, const char * a, const char *dir, const char *fname, const char *ext)
{
   if (dir && *dir)
   {
      strcpy(path, dir);
      strcat(path, SLASH_STR);
   }
   else
      *path = 0;

   strcat(path, fname);

   if (ext && *ext)
   {
      strcat(path, ".");
      strcat(path, ext);
   }
}
#endif


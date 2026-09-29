/*
 * Tempest 2000 for macOS - a small SDL2 host for the Virtual Jaguar libretro core.
 *
 * The Tempest 2000 sources (68000 + Jaguar GPU/DSP assembly) are assembled by
 * rmac/rln into t2000.abs, which this program runs on the Virtual Jaguar
 * emulation core (GPL-3, https://github.com/libretro/virtualjaguar-libretro)
 * loaded at run time through the standard libretro API.
 *
 * This host provides: window + Metal/GL video via SDL2, low-latency audio-clocked
 * pacing, keyboard (and game controller) input mapped to the Jaguar pad and keypad,
 * high-score (EEPROM) persistence, quick save states, fast-forward and fullscreen.
 *
 * Licensed GPL-3.0-or-later (it is combined with a GPL-3 core).
 */
#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <math.h>
#include "libretro.h"

#define UPSCALE 4

/* ------------------------------------------------------------- core API */
static struct {
    void *h;
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*init)(void);
    void (*deinit)(void);
    unsigned (*api_version)(void);
    void (*get_system_info)(struct retro_system_info *);
    void (*get_system_av_info)(struct retro_system_av_info *);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void (*run)(void);
    void (*reset)(void);
    size_t (*serialize_size)(void);
    bool (*serialize)(void *, size_t);
    bool (*unserialize)(const void *, size_t);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
} core;

#ifdef T2K_STATIC
/* single-binary build: the core is linked in and the game is embedded */
#include "t2000_abs.h"
static bool load_core(const char *path)
{
    (void)path;
    core.set_environment = retro_set_environment; core.set_video_refresh = retro_set_video_refresh;
    core.set_audio_sample = retro_set_audio_sample; core.set_audio_sample_batch = retro_set_audio_sample_batch;
    core.set_input_poll = retro_set_input_poll; core.set_input_state = retro_set_input_state;
    core.init = retro_init; core.deinit = retro_deinit; core.api_version = retro_api_version;
    core.get_system_info = retro_get_system_info; core.get_system_av_info = retro_get_system_av_info;
    core.load_game = retro_load_game; core.unload_game = retro_unload_game; core.run = retro_run;
    core.reset = retro_reset; core.serialize_size = retro_serialize_size; core.serialize = retro_serialize;
    core.unserialize = retro_unserialize; core.get_memory_data = retro_get_memory_data;
    core.get_memory_size = retro_get_memory_size;
    return true;
}
#else
static bool load_core(const char *path)
{
    core.h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!core.h) { fprintf(stderr, "cannot load core %s: %s\n", path, dlerror()); return false; }
#define SYM(n) do { *(void **)&core.n = dlsym(core.h, "retro_" #n); \
    if (!core.n) { fprintf(stderr, "core is missing retro_" #n "\n"); return false; } } while (0)
    SYM(set_environment); SYM(set_video_refresh); SYM(set_audio_sample); SYM(set_audio_sample_batch);
    SYM(set_input_poll); SYM(set_input_state); SYM(init); SYM(deinit); SYM(api_version);
    SYM(get_system_info); SYM(get_system_av_info); SYM(load_game); SYM(unload_game); SYM(run);
    SYM(reset); SYM(serialize_size); SYM(serialize); SYM(unserialize);
    SYM(get_memory_data); SYM(get_memory_size);
#undef SYM
    return true;
}
#endif

/* ---------------------------------------------------------------- state */
typedef struct {
    /* config */
    int scale; bool fullscreen, mute, scanlines, integer, vsync, verbose;
    const char *core_path, *game_path, *save_dir;
    int test_frames; const char *dump_path;
    /* runtime */
    SDL_Window *win; SDL_Renderer *ren;
    SDL_Texture *tex, *tex_up, *tex_scan;
    int tex_w, tex_h, up_w, up_h;
    SDL_AudioDeviceID audio; int audio_rate;
    enum retro_pixel_format pixfmt;
    unsigned fb_w, fb_h; double aspect; double fps;
    const void *last_frame; size_t last_pitch;
    bool frame_ready;
    /* input */
    uint32_t pad_mask; uint16_t keypad_mask;
    SDL_GameController *pad;
    /* scripted input for --test-frames */
    struct { int from, to; int retro_id; } script[64]; int nscript;
} G;
static G g;

static void logf_(const char *fmt, ...)
{
    if (!g.verbose) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

/* ----------------------------------------------------- libretro callbacks */
static void RETRO_CALLCONV log_cb(enum retro_log_level level, const char *fmt, ...)
{
    if (!g.verbose && level < RETRO_LOG_WARN) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

static bool RETRO_CALLCONV env_cb(unsigned cmd, void *data)
{
    switch (cmd & 0xffff) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(enum retro_pixel_format *)data;
        if (f == RETRO_PIXEL_FORMAT_XRGB8888 || f == RETRO_PIXEL_FORMAT_RGB565) { g.pixfmt = f; return true; }
        return false; }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = g.save_dir; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        /* the core's Fast blitter: about 2x faster, and renders Tempest 2000 the same; T2K_ACCURATE_BLIT=1 selects the accurate one */
        if (v->key && !strcmp(v->key, "virtualjaguar_usefastblitter") && !getenv("T2K_ACCURATE_BLIT")) { v->value = "enabled"; return true; }
        /* RISC idle-loop fast-forward: skips the GPU/DSP wait loops; bit-exact by construction (T2K_NO_IDLESKIP=1 turns it off) */
        if (v->key && !strcmp(v->key, "virtualjaguar_risc_idle_skip") && !getenv("T2K_NO_IDLESKIP")) { v->value = "enabled"; return true; }
        return false; }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS: return true;
    case RETRO_ENVIRONMENT_SET_ROTATION: return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        const struct retro_game_geometry *geo = data;
        g.fb_w = geo->base_width; g.fb_h = geo->base_height;
        g.aspect = geo->aspect_ratio > 0 ? geo->aspect_ratio : (double)geo->base_width / geo->base_height;
        return true; }
    case RETRO_ENVIRONMENT_SET_MESSAGE: fprintf(stderr, "[core] %s\n", ((struct retro_message *)data)->msg); return true;
    /* GET_VARIABLE etc.: report "not set" so the core uses its defaults */
    default: return false;
    }
}

static uint64_t hash_s = 1469598103934665603ull;
static uint64_t hash_v = 1469598103934665603ull, hash_a = 1469598103934665603ull;
static void fnv(uint64_t *h, const void *p, size_t n) { const uint8_t *b = p; while (n--) *h = (*h ^ *b++) * 1099511628211ull; }

static void RETRO_CALLCONV video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) return;
    if (g.test_frames > 0) for (unsigned y = 0; y < h; y++) fnv(&hash_v, (const uint8_t *)data + y * pitch, (size_t)w * (g.pixfmt == RETRO_PIXEL_FORMAT_RGB565 ? 2 : 4));                              /* duplicate frame */
    g.last_frame = data; g.last_pitch = pitch; g.fb_w = w; g.fb_h = h; g.frame_ready = true;
}

static void RETRO_CALLCONV audio_cb(int16_t l, int16_t r)
{
    if (g.test_frames > 0) { int16_t t[2] = { l, r }; fnv(&hash_a, t, 4); }
    int16_t s[2] = { g.mute ? 0 : l, g.mute ? 0 : r };
    if (g.audio) SDL_QueueAudio(g.audio, s, 4);
}

static size_t RETRO_CALLCONV audio_batch_cb(const int16_t *data, size_t frames)
{
    if (g.test_frames > 0) fnv(&hash_a, data, frames * 4);
    if (!g.audio) return frames;
    if (g.mute) {
        static int16_t z[4096 * 2];
        size_t n = frames > 4096 ? 4096 : frames;
        SDL_QueueAudio(g.audio, z, (Uint32)(n * 4));
    } else SDL_QueueAudio(g.audio, data, (Uint32)(frames * 4));
    return frames;
}

static void RETRO_CALLCONV input_poll_cb(void) {}

static int16_t RETRO_CALLCONV input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port != 0) return 0;
    if ((device & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)(g.pad_mask & 0xffff);
        return (g.pad_mask >> id) & 1;
    }
    if ((device & RETRO_DEVICE_MASK) == RETRO_DEVICE_KEYBOARD) {
        /* the core reads the Jaguar keypad digits from the keyboard device */
        if (id >= RETROK_0 && id <= RETROK_9) return (g.keypad_mask >> (id - RETROK_0)) & 1;
        if (id == RETROK_MINUS)  return (g.keypad_mask >> 10) & 1;   /* Jaguar '*' */
        if (id == RETROK_EQUALS) return (g.keypad_mask >> 11) & 1;   /* Jaguar '#' */
    }
    return 0;
}

/* --------------------------------------------------------------- helpers */
static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f); *len = (size_t)n; return buf;
}

/* look for a support file next to the executable, in ../Resources (app bundle) or the cwd */
static char *find_file(const char *name)
{
    char *base = SDL_GetBasePath();
    const char *dirs[] = { "", "../Resources/", "../", NULL };
    for (int i = 0; dirs[i]; i++) {
        size_t n = strlen(base ? base : "") + strlen(dirs[i]) + strlen(name) + 1;
        char *p = malloc(n); snprintf(p, n, "%s%s%s", base ? base : "", dirs[i], name);
        FILE *f = fopen(p, "rb");
        if (f) { fclose(f); SDL_free(base); return p; }
        free(p);
    }
    SDL_free(base);
    FILE *f = fopen(name, "rb");
    if (f) { fclose(f); char *p = malloc(strlen(name) + 3); sprintf(p, "./%s", name); return p; }
    return NULL;
}

static const char *base_name(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

/* ------------------------------------------------------------------ video */
static bool video_setup(void)
{
    Uint32 wf = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (g.fullscreen) wf |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    int ww = (int)(240 * g.scale * 1.3333), wh = 240 * g.scale;
    g.win = SDL_CreateWindow("Tempest 2000", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, wf);
    if (!g.win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
    g.ren = SDL_CreateRenderer(g.win, -1, SDL_RENDERER_ACCELERATED | (g.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!g.ren) g.ren = SDL_CreateRenderer(g.win, -1, 0);
    if (!g.ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return false; }
    return true;
}

static void ensure_textures(void)
{
    if (g.tex && g.tex_w == (int)g.fb_w && g.tex_h == (int)g.fb_h) return;
    if (g.tex) SDL_DestroyTexture(g.tex);
    if (g.tex_up) SDL_DestroyTexture(g.tex_up);
    if (g.tex_scan) SDL_DestroyTexture(g.tex_scan);
    g.tex_w = (int)g.fb_w; g.tex_h = (int)g.fb_h;
    g.up_w = g.tex_w * UPSCALE; g.up_h = g.tex_h * UPSCALE;
    Uint32 fmt = g.pixfmt == RETRO_PIXEL_FORMAT_RGB565 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB8888;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    g.tex = SDL_CreateTexture(g.ren, fmt, SDL_TEXTUREACCESS_STREAMING, g.tex_w, g.tex_h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    g.tex_up = SDL_CreateTexture(g.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, g.up_w, g.up_h);
    g.tex_scan = SDL_CreateTexture(g.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, g.up_w, g.up_h);
    if (g.tex_scan) {
        uint32_t *px = malloc((size_t)g.up_w * g.up_h * 4);
        if (px) {
            for (int y = 0; y < g.up_h; y++)
                for (int x = 0; x < g.up_w; x++)
                    px[(size_t)y * g.up_w + x] = (y % UPSCALE == UPSCALE - 1) ? 0x66000000u : 0u;
            SDL_UpdateTexture(g.tex_scan, NULL, px, g.up_w * 4);
            SDL_SetTextureBlendMode(g.tex_scan, SDL_BLENDMODE_BLEND);
            free(px);
        }
    }
}

static void present(void)
{
    if (!g.last_frame) return;
    ensure_textures();
    if (!g.tex || !g.tex_up) return;
    SDL_UpdateTexture(g.tex, NULL, g.last_frame, (int)g.last_pitch);
    SDL_SetRenderTarget(g.ren, g.tex_up);
    SDL_RenderCopy(g.ren, g.tex, NULL, NULL);
    if (g.scanlines && g.tex_scan) SDL_RenderCopy(g.ren, g.tex_scan, NULL, NULL);
    SDL_SetRenderTarget(g.ren, NULL);
    int ow, oh; SDL_GetRendererOutputSize(g.ren, &ow, &oh);
    SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 255); SDL_RenderClear(g.ren);
    double aspect = g.aspect > 0 ? g.aspect : (double)g.fb_w / g.fb_h;
    double w = ow, h = w / aspect;
    if (h > oh) { h = oh; w = h * aspect; }
    if (g.integer && h >= g.fb_h) { double k = floor(h / g.fb_h); h = g.fb_h * k; w = h * aspect; }
    SDL_Rect dst = { (int)((ow - w) / 2), (int)((oh - h) / 2), (int)w, (int)h };
    SDL_RenderCopy(g.ren, g.tex_up, NULL, &dst);
    SDL_RenderPresent(g.ren);
}

/* ------------------------------------------------------------------ input */
/* Jaguar pad through the RetroPad: A->A, B->B, Y->C, SELECT->Pause, START->Option,
 * X,L,R,L2,R2,L3,R3 -> keypad 0..6 (see core input table). */
static void update_input(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t m = 0;
#define BTN(id, cond) do { if (cond) m |= 1u << (id); } while (0)
    BTN(RETRO_DEVICE_ID_JOYPAD_UP,    k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]);
    BTN(RETRO_DEVICE_ID_JOYPAD_DOWN,  k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]);
    BTN(RETRO_DEVICE_ID_JOYPAD_LEFT,  k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]);
    BTN(RETRO_DEVICE_ID_JOYPAD_RIGHT, k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]);
    BTN(RETRO_DEVICE_ID_JOYPAD_A,      k[SDL_SCANCODE_Z]);                            /* Jaguar A: jump  */
    BTN(RETRO_DEVICE_ID_JOYPAD_B,      k[SDL_SCANCODE_X] || k[SDL_SCANCODE_SPACE]);   /* Jaguar B: fire  */
    BTN(RETRO_DEVICE_ID_JOYPAD_Y,      k[SDL_SCANCODE_C] || k[SDL_SCANCODE_V]);       /* Jaguar C: superzapper */
    BTN(RETRO_DEVICE_ID_JOYPAD_START,  k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_O]);  /* Option */
    BTN(RETRO_DEVICE_ID_JOYPAD_SELECT, k[SDL_SCANCODE_P] || k[SDL_SCANCODE_BACKSPACE]); /* Pause */
    uint16_t kp = 0;
    static const SDL_Scancode digits[10] = { SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
        SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9 };
    for (int i = 0; i < 10; i++) if (k[digits[i]] || k[SDL_SCANCODE_KP_0 + (i ? i - 1 : 9) ]) kp |= (uint16_t)(1u << i);
    if (k[SDL_SCANCODE_MINUS])  kp |= 1u << 10;
    if (k[SDL_SCANCODE_EQUALS]) kp |= 1u << 11;
    if (g.pad) {
        int ax = SDL_GameControllerGetAxis(g.pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(g.pad, SDL_CONTROLLER_AXIS_LEFTY);
        BTN(RETRO_DEVICE_ID_JOYPAD_UP,    SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_DPAD_UP)    || ay < -12000);
        BTN(RETRO_DEVICE_ID_JOYPAD_DOWN,  SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || ay > 12000);
        BTN(RETRO_DEVICE_ID_JOYPAD_LEFT,  SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || ax < -12000);
        BTN(RETRO_DEVICE_ID_JOYPAD_RIGHT, SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 12000);
        BTN(RETRO_DEVICE_ID_JOYPAD_A, SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_A));
        BTN(RETRO_DEVICE_ID_JOYPAD_B, SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_B));
        BTN(RETRO_DEVICE_ID_JOYPAD_Y, SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_X));
        BTN(RETRO_DEVICE_ID_JOYPAD_START,  SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_START));
        BTN(RETRO_DEVICE_ID_JOYPAD_SELECT, SDL_GameControllerGetButton(g.pad, SDL_CONTROLLER_BUTTON_BACK));
    }
#undef BTN
    /* the Jaguar pad has no simultaneous left+right */
    if ((m & (1u << RETRO_DEVICE_ID_JOYPAD_LEFT)) && (m & (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT)))
        m &= ~((1u << RETRO_DEVICE_ID_JOYPAD_LEFT) | (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT));
    g.pad_mask = m; g.keypad_mask = kp;
}

/* ---------------------------------------------------------- save / persist */
static char *eeprom_path;
static char *state_path;

static void save_eeprom(void)
{
    size_t n = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *p = core.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!n || !p || !eeprom_path) return;
    FILE *f = fopen(eeprom_path, "wb");
    if (f) { fwrite(p, 1, n, f); fclose(f); }
}

static void load_eeprom(void)
{
    size_t n = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *p = core.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!n || !p || !eeprom_path) return;
    size_t len; uint8_t *d = read_file(eeprom_path, &len);
    if (d) { memcpy(p, d, len < n ? len : n); free(d); }
}

static void quick_save(void)
{
    size_t n = core.serialize_size(); void *buf = malloc(n);
    if (buf && core.serialize(buf, n)) {
        FILE *f = fopen(state_path, "wb");
        if (f) { fwrite(buf, 1, n, f); fclose(f); fprintf(stderr, "state saved\n"); }
    }
    free(buf);
}

static void quick_load(void)
{
    size_t len; uint8_t *d = read_file(state_path, &len);
    if (!d) { fprintf(stderr, "no saved state\n"); return; }
    if (core.unserialize(d, len)) fprintf(stderr, "state loaded\n"); else fprintf(stderr, "state load failed\n");
    free(d);
}

/* ------------------------------------------------------------------- misc */
static void usage(const char *a0)
{
    printf(
"Tempest 2000 (Atari Jaguar, 1994) - macOS host for the Virtual Jaguar core\n\n"
"usage: %s [options] [other-game.abs|.rom]   (default: the built-in Tempest 2000)\n"
"  --core FILE       (multi-file build only) Virtual Jaguar libretro core\n"
"  --scale N         initial window scale (default 3)\n"
"  --fullscreen      start fullscreen\n"
"  --integer         integer-only scaling\n"
"  --scanlines       CRT scanline overlay (F4)\n"
"  --no-vsync        do not wait for the display\n"
"  --mute            start muted\n"
"  --verbose         core / host log messages\n\n"
"Jaguar pad -> keyboard\n"
"  D-pad        arrow keys or W A S D\n"
"  A / B / C    Z / Space or X / C or V         (Tempest 2000 defaults: A jump, B fire, C superzapper)\n"
"  Option       Return or O                     Pause  P or Backspace\n"
"  Keypad       0-9, - is '*', = is '#'\n"
"Emulator\n"
"  F5 save state   F7 load state   F10 reset   Tab (hold) fast-forward   M mute\n"
"  F4 scanlines    F11 / Alt+Enter fullscreen   Esc quit\n", a0);
}

static bool parse(int argc, char **argv)
{
    g.scale = 3; g.vsync = true;
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
#define NEXT() (++i < argc ? argv[i] : (fprintf(stderr, "missing value for %s\n", s), exit(2), ""))
        if (!strcmp(s, "--core")) g.core_path = NEXT();
        else if (!strcmp(s, "--scale")) g.scale = atoi(NEXT());
        else if (!strcmp(s, "--fullscreen")) g.fullscreen = true;
        else if (!strcmp(s, "--integer")) g.integer = true;
        else if (!strcmp(s, "--scanlines")) g.scanlines = true;
        else if (!strcmp(s, "--no-vsync")) g.vsync = false;
        else if (!strcmp(s, "--mute")) g.mute = true;
        else if (!strcmp(s, "--verbose")) g.verbose = true;
        else if (!strcmp(s, "--save-dir")) g.save_dir = NEXT();
        else if (!strcmp(s, "--test-frames")) g.test_frames = atoi(NEXT());     /* hidden */
        else if (!strcmp(s, "--dump")) g.dump_path = NEXT();                     /* hidden: prefix */
        else if (!strcmp(s, "--press")) {                                        /* hidden: FROM:TO:ID */
            int a, b, id;
            if (sscanf(NEXT(), "%d:%d:%d", &a, &b, &id) == 3 && g.nscript < 64) {
                g.script[g.nscript].from = a; g.script[g.nscript].to = b; g.script[g.nscript].retro_id = id; g.nscript++;
            }
        }
        else if (!strcmp(s, "--help") || !strcmp(s, "-h")) { usage(argv[0]); exit(0); }
        else if (s[0] == '-') { fprintf(stderr, "unknown option %s\n", s); usage(argv[0]); return false; }
        else g.game_path = s;
    }
    if (g.scale < 1) g.scale = 1;
    return true;
}

static void set_title(double fps, bool turbo)
{
    char t[96];
    snprintf(t, sizeof t, "Tempest 2000%s%s  [%.1f fps]", g.mute ? " - muted" : "", turbo ? " - FAST" : "", fps);
    SDL_SetWindowTitle(g.win, t);
}

static void dump_bmp(const char *path)
{
    if (!g.last_frame) return;
    Uint32 fmt = g.pixfmt == RETRO_PIXEL_FORMAT_RGB565 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB8888;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom((void *)g.last_frame, (int)g.fb_w, (int)g.fb_h,
                        g.pixfmt == RETRO_PIXEL_FORMAT_RGB565 ? 16 : 32, (int)g.last_pitch, fmt);
    if (s) { SDL_SaveBMP(s, path); SDL_FreeSurface(s); }
}

/* ------------------------------------------------------------------- main */
int main(int argc, char **argv)
{
    if (!parse(argc, argv)) return 2;

    const char *core_name =
#ifdef __APPLE__
        "virtualjaguar_libretro.dylib";
#else
        "virtualjaguar_libretro.so";
#endif
#ifdef T2K_STATIC
    (void)core_name;
#else
    if (!g.core_path) { g.core_path = find_file(core_name); if (!g.core_path) { static char cp[256]; snprintf(cp, sizeof cp, "./%s", core_name); g.core_path = cp; } }
#endif
#ifndef T2K_STATIC
    if (!g.game_path) { g.game_path = find_file("t2000.abs"); if (!g.game_path) g.game_path = "t2000.abs"; }
#else
    if (!g.game_path) g.game_path = "t2000.abs";        /* name only; data is embedded */
#endif
    if (!g.save_dir) {
        const char *home = getenv("HOME");
        static char dir[1024];
#ifdef __APPLE__
        snprintf(dir, sizeof dir, "%s/Library/Application Support/Tempest2000", home ? home : ".");
        char parent[1024]; snprintf(parent, sizeof parent, "%s/Library/Application Support", home ? home : ".");
        mkdir(parent, 0755);
#else
        snprintf(dir, sizeof dir, "%s/.tempest2000", home ? home : ".");
#endif
        mkdir(dir, 0755);
        g.save_dir = dir;
    }
    if (!load_core(g.core_path)) return 1;
    if (core.api_version() != RETRO_API_VERSION) { fprintf(stderr, "libretro API mismatch\n"); return 1; }

    g.pixfmt = RETRO_PIXEL_FORMAT_RGB565;
    core.set_environment(env_cb);
    core.init();
    core.set_video_refresh(video_cb);
    core.set_audio_sample(audio_cb);
    core.set_audio_sample_batch(audio_batch_cb);
    core.set_input_poll(input_poll_cb);
    core.set_input_state(input_state_cb);

    size_t glen = 0; uint8_t *gdata = NULL;
#ifdef T2K_STATIC
    if (!strcmp(g.game_path, "t2000.abs")) { gdata = (uint8_t *)t2000_abs; glen = sizeof t2000_abs; }
    else
#endif
    gdata = read_file(g.game_path, &glen);
    if (!gdata) { fprintf(stderr, "cannot read %s (build it first: see README)\n", g.game_path); return 1; }
    struct retro_game_info info = { g.game_path, gdata, glen, NULL };
    if (!core.load_game(&info)) { fprintf(stderr, "core rejected %s\n", g.game_path); return 1; }
    struct retro_system_av_info av; core.get_system_av_info(&av);
    g.fb_w = av.geometry.base_width; g.fb_h = av.geometry.base_height;
    g.aspect = av.geometry.aspect_ratio > 0 ? av.geometry.aspect_ratio : (double)g.fb_w / g.fb_h;
    g.fps = av.timing.fps > 1 ? av.timing.fps : 60.0;
    int rate = (int)(av.timing.sample_rate + 0.5); if (rate < 8000) rate = 48000;
    logf_("core: %ux%u aspect %.4f fps %.3f audio %d Hz\n", g.fb_w, g.fb_h, g.aspect, g.fps, rate);

    static char ep[1200], sp[1200];
    snprintf(ep, sizeof ep, "%s/%s.eeprom", g.save_dir, base_name(g.game_path)); eeprom_path = ep;
    snprintf(sp, sizeof sp, "%s/%s.state", g.save_dir, base_name(g.game_path)); state_path = sp;
    load_eeprom();

    /* ------------------------------------------------ headless self test */
    if (g.test_frames > 0) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
        SDL_Init(SDL_INIT_VIDEO);
        for (int f = 0; f < g.test_frames; f++) {
            uint32_t m = 0;
            for (int i = 0; i < g.nscript; i++)
                if (f >= g.script[i].from && f < g.script[i].to) m |= 1u << g.script[i].retro_id;
            g.pad_mask = m;
            core.run();
            if ((f + 1) % 100 == 0) { size_t n = core.serialize_size(); void *b = malloc(n); if (b && core.serialize(b, n)) fnv(&hash_s, b, n); free(b); }
            if (g.dump_path && g.frame_ready && ((f + 1) % 100 == 0)) {
                char name[1024]; snprintf(name, sizeof name, "%s%05d.bmp", g.dump_path, f + 1);
                dump_bmp(name);
            }
        }
        printf("test ok: %d frames, %ux%u  video %016llx audio %016llx state %016llx\n", g.test_frames, g.fb_w, g.fb_h, (unsigned long long)hash_v, (unsigned long long)hash_a, (unsigned long long)hash_s);
        core.unload_game(); core.deinit();
        return 0;
    }

    /* ------------------------------------------------------------- SDL */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    if (!video_setup()) return 1;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want, have; SDL_zero(want);
        want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
        g.audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (g.audio) SDL_PauseAudioDevice(g.audio, 0);
        else fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { g.pad = SDL_GameControllerOpen(i); if (g.pad) break; }
    SDL_ShowCursor(g.fullscreen ? SDL_DISABLE : SDL_ENABLE);

    Uint64 freq = SDL_GetPerformanceFrequency(), last_title = SDL_GetPerformanceCounter(), next_frame = last_title;
    const Uint32 target_queue = (Uint32)(rate / g.fps * 3.0) * 4;   /* ~3 frames of audio, bytes */
    int frames_in_title = 0; bool running = true, turbo = false;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_CONTROLLERDEVICEADDED && !g.pad) g.pad = SDL_GameControllerOpen(e.cdevice.which);
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_ESCAPE: running = false; break;
                case SDL_SCANCODE_M: g.mute = !g.mute; if (g.audio) SDL_ClearQueuedAudio(g.audio); break;
                case SDL_SCANCODE_F4: g.scanlines = !g.scanlines; break;
                case SDL_SCANCODE_F5: quick_save(); break;
                case SDL_SCANCODE_F7: quick_load(); break;
                case SDL_SCANCODE_F10: core.reset(); break;
                case SDL_SCANCODE_F11:
                    g.fullscreen = !g.fullscreen;
                    SDL_SetWindowFullscreen(g.win, g.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_ShowCursor(g.fullscreen ? SDL_DISABLE : SDL_ENABLE); break;
                case SDL_SCANCODE_RETURN:
                    if (e.key.keysym.mod & KMOD_ALT) {
                        g.fullscreen = !g.fullscreen;
                        SDL_SetWindowFullscreen(g.win, g.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_ShowCursor(g.fullscreen ? SDL_DISABLE : SDL_ENABLE);
                    }
                    break;
                default: break;
                }
            }
        }
        turbo = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_TAB] != 0;
        update_input();
        if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_RETURN] && (SDL_GetModState() & KMOD_ALT))
            g.pad_mask &= ~(1u << RETRO_DEVICE_ID_JOYPAD_START);

        int produced = 0;
        if (turbo) {
            for (int i = 0; i < 4; i++) core.run();
            if (g.audio) SDL_ClearQueuedAudio(g.audio);
            produced = 4;
        } else if (g.audio) {
            while (produced < 3 && SDL_GetQueuedAudioSize(g.audio) < target_queue) { core.run(); produced++; }
            if (!produced) SDL_Delay(1);
        } else {
            Uint64 now = SDL_GetPerformanceCounter(); double period = (double)freq / g.fps;
            while (produced < 3 && now >= next_frame) { core.run(); produced++; next_frame += (Uint64)period; }
            if (now > next_frame + (Uint64)(period * 4)) next_frame = now;
            if (!produced) SDL_Delay(1);
        }
        if (produced) { present(); frames_in_title += produced; }

        Uint64 now = SDL_GetPerformanceCounter();
        if (now - last_title >= freq) {
            double fps = frames_in_title * (double)freq / (double)(now - last_title);
            set_title(fps, turbo);
            if (getenv("T2K_DEBUG")) fprintf(stderr, "%.2f fps\n", fps);
            frames_in_title = 0; last_title = now;
        }
    }

    save_eeprom();
    core.unload_game(); core.deinit();
    if (g.pad) SDL_GameControllerClose(g.pad);
    if (g.audio) SDL_CloseAudioDevice(g.audio);
    SDL_Quit();
    return 0;
}

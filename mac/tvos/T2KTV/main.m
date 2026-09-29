/* Tempest 2000 for Apple TV: a small UIKit front end for the Virtual Jaguar libretro core
 * (linked in statically).  It runs the original t2000.abs, shows the video in a CALayer,
 * plays the audio through AVAudioEngine and reads a Siri Remote or any game controller. */
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>
#include <stdatomic.h>
#include <os/lock.h>
#include <unistd.h>
#include <string.h>
#include "libretro.h"

/* ------------------------------------------------------------- emulator glue */
static struct {
    enum retro_pixel_format pixfmt;
    unsigned w, h; double aspect, fps; int rate;
    const void *frame; size_t pitch; bool ready;
    uint32_t pad;
    const char *dir;
} g = { RETRO_PIXEL_FORMAT_RGB565, 320, 240, 4.0 / 3.0, 60.0, 48000, NULL, 0, false, 0, NULL };

/* audio ring: single producer (main thread), single consumer (audio thread) */
#define RING (1 << 15)
static int16_t ring[RING * 2];
static _Atomic uint32_t rd, wr;
static bool muted;
static _Atomic uint32_t underruns;
static uint32_t ring_fill(void) { return atomic_load(&wr) - atomic_load(&rd); }
static size_t audio_batch(const int16_t *d, size_t n) {
    uint32_t w = atomic_load(&wr), r = atomic_load(&rd);
    for (size_t i = 0; i < n; i++) {
        if (w - r >= RING) break;
        ring[(w & (RING - 1)) * 2] = muted ? 0 : d[i * 2]; ring[(w & (RING - 1)) * 2 + 1] = muted ? 0 : d[i * 2 + 1]; w++;
    }
    atomic_store(&wr, w); return n;
}
static void audio_one(int16_t l, int16_t r) { int16_t s[2] = { l, r }; audio_batch(s, 1); }

static void video_cb(const void *d, unsigned w, unsigned h, size_t p) { if (!d) return; g.frame = d; g.pitch = p; g.w = w; g.h = h; g.ready = true; }
static void poll_cb(void) {}
static int16_t input_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    if (port || (dev & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)(g.pad & 0xffff);
    return (g.pad >> id) & 1;
}
static bool env_cb(unsigned cmd, void *data) {
    switch (cmd & 0xffff) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(enum retro_pixel_format *)data;
        if (f == RETRO_PIXEL_FORMAT_XRGB8888 || f == RETRO_PIXEL_FORMAT_RGB565) { g.pixfmt = f; return true; }
        return false; }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = g.dir; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {     /* the core's 'Fast' blitter: ~2x faster overall, renders Tempest 2000 the same */
        struct retro_variable *v = data;
        if (v->key && !strcmp(v->key, "virtualjaguar_usefastblitter")) { v->value = "enabled"; return true; }
        /* RISC idle-loop fast-forward: skips GPU/DSP wait loops; verified bit-exact (identical frame + audio hashes) */
        if (v->key && !strcmp(v->key, "virtualjaguar_risc_idle_skip")) { v->value = "enabled"; return true; }
        return false; }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_ROTATION:
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        const struct retro_game_geometry *geo = data;
        g.aspect = geo->aspect_ratio > 0 ? geo->aspect_ratio : (double)geo->base_width / geo->base_height; return true; }
    default: return false;
    }
}

/* High scores (the cartridge EEPROM) live in NSUserDefaults: tvOS has no persistent file storage. */
static NSString *const kEEPROM = @"eeprom";
static void eeprom_load(void) {
    size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM); void *p = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    NSData *d = [NSUserDefaults.standardUserDefaults dataForKey:kEEPROM];
    if (n && p && d) memcpy(p, d.bytes, MIN(n, d.length));
}
static NSData *last_eeprom;
static void eeprom_save(void) {
    size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM); void *p = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!n || !p) return;
    NSData *d = [NSData dataWithBytes:p length:n];
    if ([d isEqualToData:last_eeprom]) return;
    last_eeprom = d; [NSUserDefaults.standardUserDefaults setObject:d forKey:kEEPROM];
}

/* ------------------------------------------------------------------ input */
static void update_input(void) {
    uint32_t m = 0;
#define B(id, cond) if (cond) m |= 1u << (id)
    for (GCController *c in GCController.controllers) {
        GCExtendedGamepad *x = c.extendedGamepad; GCMicroGamepad *u = c.microGamepad;
        if (x) {
            float ax = x.leftThumbstick.xAxis.value, ay = x.leftThumbstick.yAxis.value;
            B(RETRO_DEVICE_ID_JOYPAD_UP,    x.dpad.up.isPressed    || ay >  0.5f);
            B(RETRO_DEVICE_ID_JOYPAD_DOWN,  x.dpad.down.isPressed  || ay < -0.5f);
            B(RETRO_DEVICE_ID_JOYPAD_LEFT,  x.dpad.left.isPressed  || ax < -0.5f);
            B(RETRO_DEVICE_ID_JOYPAD_RIGHT, x.dpad.right.isPressed || ax >  0.5f);
            B(RETRO_DEVICE_ID_JOYPAD_B, x.buttonA.isPressed);                        /* Jaguar B: fire */
            B(RETRO_DEVICE_ID_JOYPAD_A, x.buttonB.isPressed);                        /* Jaguar A: jump */
            B(RETRO_DEVICE_ID_JOYPAD_Y, x.buttonX.isPressed || x.buttonY.isPressed
                                        || x.leftShoulder.isPressed || x.rightShoulder.isPressed
                                        || x.rightTrigger.isPressed);                /* Jaguar C: superzapper */
            B(RETRO_DEVICE_ID_JOYPAD_START, x.buttonMenu.isPressed);                 /* Option */
            B(RETRO_DEVICE_ID_JOYPAD_SELECT, x.buttonOptions.isPressed);             /* Pause */
        } else if (u) {
            u.allowsRotation = YES; u.reportsAbsoluteDpadValues = NO;
            float ax = u.dpad.xAxis.value, ay = u.dpad.yAxis.value;
            B(RETRO_DEVICE_ID_JOYPAD_UP,    u.dpad.up.isPressed    || ay >  0.5f);
            B(RETRO_DEVICE_ID_JOYPAD_DOWN,  u.dpad.down.isPressed  || ay < -0.5f);
            B(RETRO_DEVICE_ID_JOYPAD_LEFT,  u.dpad.left.isPressed  || ax < -0.5f);
            B(RETRO_DEVICE_ID_JOYPAD_RIGHT, u.dpad.right.isPressed || ax >  0.5f);
            B(RETRO_DEVICE_ID_JOYPAD_B, u.buttonA.isPressed);                        /* click: fire */
            B(RETRO_DEVICE_ID_JOYPAD_A, u.buttonX.isPressed);                        /* play/pause: jump */
            B(RETRO_DEVICE_ID_JOYPAD_START, u.buttonMenu.isPressed);                 /* menu: Option */
        }
    }
    g.pad = m;
}

/* ------------------------------------------------------------------ view */
@interface T2KController : GCEventViewController
@end
@implementation T2KController {
    CALayer *_screen; CADisplayLink *_link; AVAudioEngine *_engine;
    uint32_t *_pix; size_t _pixcap; int _frames; CFTimeInterval _t0; bool _running;
    NSLock *_emu; NSThread *_thread; bool _quit;             /* emulation thread: retro_run() is only called with _emu held */
    os_unfair_lock _fl; NSData *_latest; unsigned _lw, _lh;   /* newest converted frame, handed to the main thread */
    int _mruns, _mlate; double _msum, _mmax; uint32_t _munder;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.controllerUserInteractionEnabled = NO;       /* deliver Menu / Play-Pause to the game, not the OS */
    self.view.backgroundColor = UIColor.blackColor;
    _screen = [CALayer layer]; _screen.magnificationFilter = kCAFilterLinear; _screen.opaque = YES;
    [self.view.layer addSublayer:_screen];

    NSString *path = [NSBundle.mainBundle pathForResource:@"t2000" ofType:@"abs"];
    NSData *rom = [NSData dataWithContentsOfFile:path];
    NSString *dir = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    g.dir = strdup(dir.fileSystemRepresentation);
    NSLog(@"T2K: rom %lu bytes, cache dir %@", (unsigned long)rom.length, dir);
    if (!rom) { [self fail:@"t2000.abs missing from the app bundle"]; return; }

    retro_set_environment(env_cb); retro_init();
    retro_set_video_refresh(video_cb); retro_set_audio_sample(audio_one); retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(poll_cb); retro_set_input_state(input_cb);
    struct retro_game_info gi = { "t2000.abs", rom.bytes, rom.length, NULL };
    if (!retro_load_game(&gi)) { [self fail:@"the emulator core rejected the game"]; return; }
    struct retro_system_av_info av; retro_get_system_av_info(&av);
    g.w = av.geometry.base_width; g.h = av.geometry.base_height;
    g.aspect = av.geometry.aspect_ratio > 0 ? av.geometry.aspect_ratio : (double)g.w / g.h;
    g.fps = av.timing.fps > 1 ? av.timing.fps : 60.0;
    g.rate = (int)(av.timing.sample_rate + 0.5); if (g.rate < 8000) g.rate = 48000;
    NSLog(@"T2K: core loaded %ux%u aspect %.3f fps %.2f audio %d Hz pixfmt %d", g.w, g.h, g.aspect, g.fps, g.rate, g.pixfmt);
    eeprom_load(); last_eeprom = nil;
    [self startAudio];
    _running = true; _t0 = CACurrentMediaTime(); _emu = [NSLock new]; _fl = OS_UNFAIR_LOCK_INIT;
#ifndef T2K_MAIN_THREAD
    _thread = [[NSThread alloc] initWithTarget:self selector:@selector(emuLoop) object:nil];
    _thread.name = @"T2K emulation"; _thread.qualityOfService = NSQualityOfServiceUserInteractive; [_thread start];
#endif
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    _link.preferredFramesPerSecond = 60;
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
    [nc addObserver:self selector:@selector(pause) name:UIApplicationWillResignActiveNotification object:nil];
    [nc addObserver:self selector:@selector(resume) name:UIApplicationDidBecomeActiveNotification object:nil];
    [nc addObserver:self selector:@selector(pause) name:UIApplicationDidEnterBackgroundNotification object:nil];
}
- (void)fail:(NSString *)msg {
    UILabel *l = [[UILabel alloc] initWithFrame:self.view.bounds]; l.text = msg; l.textColor = UIColor.whiteColor;
    l.textAlignment = NSTextAlignmentCenter; [self.view addSubview:l];
}
- (void)startAudio {
    NSError *e = nil;
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryPlayback error:&e];
    [AVAudioSession.sharedInstance setActive:YES error:&e];
    _engine = [AVAudioEngine new];
    AVAudioFormat *fmt = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:g.rate channels:2 interleaved:NO];
    AVAudioSourceNode *src = [[AVAudioSourceNode alloc] initWithFormat:fmt renderBlock:
        ^OSStatus(BOOL *silence, const AudioTimeStamp *ts, AVAudioFrameCount n, AudioBufferList *abl) {
            float *L = abl->mBuffers[0].mData, *R = abl->mNumberBuffers > 1 ? abl->mBuffers[1].mData : L;
            uint32_t r = atomic_load(&rd), avail = atomic_load(&wr) - r;
            for (AVAudioFrameCount i = 0; i < n; i++) {
                if (i < avail) { const int16_t *s = &ring[(r & (RING - 1)) * 2]; L[i] = s[0] / 32768.f; R[i] = s[1] / 32768.f; r++; }
                else L[i] = R[i] = 0;
            }
            if (avail < n) atomic_fetch_add(&underruns, 1);
            atomic_store(&rd, r); return noErr;
        }];
    [_engine attachNode:src]; [_engine connect:src to:_engine.mainMixerNode format:fmt];
    if (![_engine startAndReturnError:&e]) NSLog(@"audio: %@", e);
}
- (void)pause { if (!_running) return; [_emu lock]; _running = false; eeprom_save(); [_emu unlock]; _link.paused = YES; [_engine pause]; }
- (void)resume { if (_running || !_link) return; [_emu lock]; _running = true; [_emu unlock]; _link.paused = NO; [_engine startAndReturnError:nil]; }
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGSize b = self.view.bounds.size; CGFloat w = b.width, h = w / g.aspect;
    if (h > b.height) { h = b.height; w = h * g.aspect; }
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.frame = CGRectMake((b.width - w) / 2, (b.height - h) / 2, w, h); [CATransaction commit];
}
/* --- performance log: emulated-frame cost, every 600 frames --- */
- (void)note:(double)ms {
    _msum += ms; if (ms > _mmax) _mmax = ms; if (ms > 1000.0 / g.fps) _mlate++;
    if (++_mruns == 600) {
        uint32_t u = atomic_load(&underruns);
        NSLog(@"T2K: perf 600 frames | core avg %.2f ms max %.2f ms | over budget %d | audio underruns %u", _msum / _mruns, _mmax, _mlate, u - _munder);
        _munder = u; _mruns = _mlate = 0; _msum = _mmax = 0;
    }
}
- (NSData *)convert {                  /* current libretro frame -> BGRA pixels (called right after retro_run) */
    size_t n = (size_t)g.w * g.h; NSMutableData *d = [NSMutableData dataWithLength:n * 4]; uint32_t *px = d.mutableBytes;
    for (unsigned y = 0; y < g.h; y++) {
        uint32_t *o = px + (size_t)y * g.w;
        if (g.pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
            const uint16_t *s = (const uint16_t *)((const uint8_t *)g.frame + y * g.pitch);
            for (unsigned x = 0; x < g.w; x++) {
                uint32_t p = s[x], r = (p >> 11) & 31, gg = (p >> 5) & 63, b = p & 31;
                o[x] = 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((gg << 2) | (gg >> 4)) << 8) | ((b << 3) | (b >> 2));
            }
        } else {
            const uint32_t *s = (const uint32_t *)((const uint8_t *)g.frame + y * g.pitch);
            for (unsigned x = 0; x < g.w; x++) o[x] = s[x] | 0xff000000u;
        }
    }
    return d;
}
- (void)publish {
    NSData *d = [self convert]; os_unfair_lock_lock(&_fl); _latest = d; _lw = g.w; _lh = g.h; os_unfair_lock_unlock(&_fl); g.ready = false;
}
- (void)emuLoop {                      /* runs the emulator flat out until the audio queue holds ~3 frames */
    const uint32_t target = (uint32_t)(g.rate / g.fps * 3.0);
    while (!_quit) {
        [_emu lock];
        if (_running && ring_fill() < target) {
            CFTimeInterval a = CACurrentMediaTime(); retro_run();
            if (g.ready) [self publish];
            [self note:1000 * (CACurrentMediaTime() - a)]; [_emu unlock];
        } else { [_emu unlock]; usleep(1000); }
    }
}
- (void)tick:(CADisplayLink *)l {
    update_input();
#ifdef T2K_MAIN_THREAD
    const uint32_t target = (uint32_t)(g.rate / g.fps * 3.0);
    int produced = 0;
    while (produced < 3 && ring_fill() < target) { CFTimeInterval a = CACurrentMediaTime(); retro_run(); if (g.ready) [self publish]; [self note:1000 * (CACurrentMediaTime() - a)]; produced++; }
#endif
    os_unfair_lock_lock(&_fl); NSData *d = _latest; _latest = nil; unsigned w = _lw, h = _lh; os_unfair_lock_unlock(&_fl);
    if (d) [self show:d width:w height:h];
    if (++_frames % 300 == 0) { [_emu lock]; eeprom_save(); [_emu unlock]; }
}
- (void)show:(NSData *)d width:(unsigned)w height:(unsigned)h {
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef dp = CGDataProviderCreateWithCFData((__bridge CFDataRef)d);
    CGImageRef img = CGImageCreate(w, h, 8, 32, w * 4, cs, kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, dp, NULL, false, kCGRenderingIntentDefault);
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.contents = (__bridge id)img; [CATransaction commit];
    CGImageRelease(img); CGDataProviderRelease(dp); CGColorSpaceRelease(cs);
    static unsigned lw, lh; if (lw != w || lh != h) { lw = w; lh = h; [self.view setNeedsLayout]; }
}
@end


@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)a didFinishLaunchingWithOptions:(NSDictionary *)o {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [T2KController new];
    [self.window makeKeyAndVisible];
    a.idleTimerDisabled = YES;
    return YES;
}
- (void)applicationWillTerminate:(UIApplication *)a { eeprom_save(); }
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class)); }
}

// Glitchfest, a glitch audio effect as a WebCLAP. Twelve effect rows, four patterns of 32 columns where drawn blocks
// switch an effect on, a sequence of eight pattern slots that follows the host transport. openDAW shows the page in
// its device panel (feature "opendaw:device-panel", extension "opendaw.device-panel/1"), every host in the window.

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>
#include <clap/ext/draft/param-hovered.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

extern "C" void emscripten_notify_memory_growth(size_t index) {(void) index;}

enum Effect {E_STUTTER, E_SHUFFLE, E_REVERSE, E_SCRATCH, E_FREEZE, E_TAPE, E_PITCH, E_CRUSH, E_FILTER, E_GATE, E_PAN, E_THROW, ROWS};

enum : clap_id {
    P_MIX, P_LENGTH, P_DICE,
    P_STUTTER_DIV, P_STUTTER_DECAY, P_SHUFFLE_SLICE, P_SHUFFLE_SEED, P_REVERSE_SIZE, P_REVERSE_FADE,
    P_SCRATCH_RATE, P_SCRATCH_DEPTH, P_FREEZE_SIZE, P_FREEZE_JITTER, P_TAPE_MODE, P_TAPE_CURVE,
    P_PITCH_SEMI, P_PITCH_GLIDE, P_CRUSH_BITS, P_CRUSH_RATE, P_FILTER_DIR, P_FILTER_RESO,
    P_GATE_DIV, P_GATE_DUTY, P_PAN_RATE, P_PAN_WIDTH, P_THROW_TIME, P_THROW_FEEDBACK,
    PARAM_COUNT
};

enum Curve : uint8_t {LINEAR, EXPONENTIAL, STEPPED};

enum Unit : uint8_t {U_PERCENT, U_MS, U_HZ, U_TIMES, U_SEMITONES, U_BITS, U_PLAIN, U_CHOICE};

struct ParamDef {
    const char *name;
    const char *module;
    double min, max, value;
    Curve curve;
    Unit unit;
    const char *const *texts;
};

static const char *LENGTH_TEXTS[] = {"1 bar", "2 bars", "4 bars"};
static const char *STUTTER_TEXTS[] = {"1/4", "1/8", "1/16", "1/32", "1/64"};
static const char *SLICE_TEXTS[] = {"1/4", "1/8", "1/16", "1/32"};
static const char *REVERSE_TEXTS[] = {"1/2", "1/4", "1/8", "1/16"};
static const char *TAPE_TEXTS[] = {"stop", "start"};
static const char *DIRECTION_TEXTS[] = {"down", "up"};
static const char *GATE_TEXTS[] = {"1/8", "1/16", "1/32", "1/64"};
static const char *PAN_TEXTS[] = {"1/2", "1/4", "1/8", "1/16"};
static const char *THROW_TEXTS[] = {"1/16", "1/8", "3/16", "1/4", "3/8"};
static const double STUTTER_BEATS[] = {1.0, 0.5, 0.25, 0.125, 0.0625};
static const double SLICE_BEATS[] = {1.0, 0.5, 0.25, 0.125};
static const double REVERSE_BEATS[] = {2.0, 1.0, 0.5, 0.25};
static const double GATE_BEATS[] = {0.5, 0.25, 0.125, 0.0625};
static const double PAN_BEATS[] = {2.0, 1.0, 0.5, 0.25};
static const double THROW_BEATS[] = {0.25, 0.5, 0.75, 1.0, 1.5};

// the page (page/index.html) keeps the same table, ids are the indices
static const ParamDef DEFS[PARAM_COUNT] = {
    {"Mix", "Global", 0.0, 1.0, 1.0, LINEAR, U_PERCENT, nullptr},
    {"Length", "Global", 0.0, 2.0, 0.0, STEPPED, U_CHOICE, LENGTH_TEXTS},
    {"Dice", "Global", 0.0, 1.0, 0.35, LINEAR, U_PERCENT, nullptr},
    {"Stutter Division", "Stutter", 0.0, 4.0, 2.0, STEPPED, U_CHOICE, STUTTER_TEXTS},
    {"Stutter Decay", "Stutter", 0.0, 1.0, 0.4, LINEAR, U_PERCENT, nullptr},
    {"Shuffle Slice", "Shuffle", 0.0, 3.0, 1.0, STEPPED, U_CHOICE, SLICE_TEXTS},
    {"Shuffle Seed", "Shuffle", 0.0, 99.0, 17.0, STEPPED, U_PLAIN, nullptr},
    {"Reverse Size", "Reverse", 0.0, 3.0, 1.0, STEPPED, U_CHOICE, REVERSE_TEXTS},
    {"Reverse Fade", "Reverse", 1.0, 50.0, 8.0, EXPONENTIAL, U_MS, nullptr},
    {"Scratch Rate", "Scratch", 0.25, 4.0, 1.0, EXPONENTIAL, U_TIMES, nullptr},
    {"Scratch Depth", "Scratch", 0.0, 1.0, 0.6, LINEAR, U_PERCENT, nullptr},
    {"Freeze Size", "Freeze", 10.0, 500.0, 60.0, EXPONENTIAL, U_MS, nullptr},
    {"Freeze Jitter", "Freeze", 0.0, 1.0, 0.3, LINEAR, U_PERCENT, nullptr},
    {"Tape Mode", "Tape", 0.0, 1.0, 0.0, STEPPED, U_CHOICE, TAPE_TEXTS},
    {"Tape Curve", "Tape", 0.0, 1.0, 0.5, LINEAR, U_PERCENT, nullptr},
    {"Pitch Semitones", "Pitch", -24.0, 24.0, -12.0, STEPPED, U_SEMITONES, nullptr},
    {"Pitch Glide", "Pitch", 0.0, 1.0, 0.0, LINEAR, U_PERCENT, nullptr},
    {"Crush Bits", "Crush", 1.0, 16.0, 6.0, STEPPED, U_BITS, nullptr},
    {"Crush Rate", "Crush", 500.0, 48000.0, 8000.0, EXPONENTIAL, U_HZ, nullptr},
    {"Filter Direction", "Filter", 0.0, 1.0, 0.0, STEPPED, U_CHOICE, DIRECTION_TEXTS},
    {"Filter Resonance", "Filter", 0.0, 1.0, 0.7, LINEAR, U_PERCENT, nullptr},
    {"Gate Division", "Gate", 0.0, 3.0, 2.0, STEPPED, U_CHOICE, GATE_TEXTS},
    {"Gate Duty", "Gate", 0.0, 1.0, 0.5, LINEAR, U_PERCENT, nullptr},
    {"Pan Rate", "Pan", 0.0, 3.0, 2.0, STEPPED, U_CHOICE, PAN_TEXTS},
    {"Pan Width", "Pan", 0.0, 1.0, 1.0, LINEAR, U_PERCENT, nullptr},
    {"Throw Time", "Throw", 0.0, 4.0, 2.0, STEPPED, U_CHOICE, THROW_TEXTS},
    {"Throw Feedback", "Throw", 0.0, 0.95, 0.55, LINEAR, U_PERCENT, nullptr}
};

#define PATTERNS 4
#define COLUMNS 32
#define SLOTS 8
#define HISTORY_SECONDS 20
#define THROW_SECONDS 4
#define FADE_SECONDS 0.005
#define OUT_EVENTS 128
#define GUI_WIDTH 244
#define GUI_HEIGHT 176
#define GUI_MIN_SCALE 1.0
#define GUI_MAX_SCALE 3.0
#define STATE_MAGIC 0x46544c47u
#define STATE_VERSION 2u

// message types, see page/index.html
#define PAGE_POLL 1
#define PAGE_PARAM_BEGIN 2
#define PAGE_PARAM_SET 3
#define PAGE_PARAM_END 4
#define PAGE_HOVER 5
#define PAGE_SET_ROW 6
#define PAGE_SET_SEQUENCE 7
#define PAGE_REQUEST_PATTERNS 8
#define PAGE_SET_ORDER 9
#define PLUGIN_STATUS 1
#define PLUGIN_PATTERNS 2
#define STATUS_HEADER 24
#define ROW_BYTES (4 + COLUMNS * 4)
#define PATTERNS_HEADER 32

#define OPENDAW_PLUGIN_FEATURE_DEVICE_PANEL "opendaw:device-panel"
#define OPENDAW_EXT_DEVICE_PANEL "opendaw.device-panel/1"

typedef struct opendaw_plugin_device_panel {
    int32_t (*get_uri)(const clap_plugin_t *plugin, char *uri, uint32_t capacity);
    bool (*get_width)(const clap_plugin_t *plugin, uint32_t height, uint32_t *width);
} opendaw_plugin_device_panel_t;

static const double TAU = 6.283185307179586;

struct Block {
    uint8_t start, length, chance, ramp;
};

struct Row {
    uint32_t count;
    Block blocks[COLUMNS];
};

struct Pattern {
    Row rows[ROWS];
};

struct Lane {
    int64_t key;
    bool armed;
    double start_beat, length_beats, ramp, amount;
    double phase, hold[2], gate;
    double ic1[2], ic2[2];
    int64_t grain_index[2];
    double grain_offset[2];
};

struct OutEvent {
    uint16_t type;
    clap_id id;
    double value;
};

struct Glitchfest {
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_params_t *host_params;
    const clap_host_state_t *host_state;
    const clap_host_webview_t *host_webview;
    const clap_host_param_hovered_t *host_hovered;
    double values[PARAM_COUNT];
    Pattern patterns[PATTERNS];
    int8_t sequence[SLOTS];
    uint8_t order[ROWS];
    std::atomic<uint32_t> generation;
    uint32_t seen_generation;
    double sample_rate;
    float *history[2];
    uint32_t history_capacity;
    float *throw_line[2];
    uint32_t throw_capacity, throw_write;
    double throw_damp[2];
    uint64_t clock;
    double tempo, free_beats, last_beat;
    bool playing;
    int64_t column_index;
    int slot;
    double column;
    Lane lanes[ROWS];
    Lane fading[ROWS];
    double fade[ROWS];
    uint32_t random;
    uint32_t gui_width, gui_height;
    OutEvent out_events[OUT_EVENTS];
    uint32_t out_read, out_write;
};

static const char *features[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_GLITCH, CLAP_PLUGIN_FEATURE_DELAY,
    CLAP_PLUGIN_FEATURE_STEREO, OPENDAW_PLUGIN_FEATURE_DEVICE_PANEL, nullptr
};

static const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT,
    "studio.opendaw.commons.glitchfest",
    "Glitchfest",
    "openDAW Commons",
    "https://github.com/andremichelle/opendaw-commons/tree/main/wclaps/glitchfest",
    "",
    "",
    "1.0.0",
    "A glitch effect: twelve effect rows, blocks drawn in four patterns, a pattern sequence on the host transport",
    features
};

static Glitchfest *glitchfest_of(const clap_plugin_t *plugin) {return static_cast<Glitchfest *>(plugin->plugin_data);}

static double clamp(double value, double min, double max) {return value < min ? min : value > max ? max : value;}

static double clamp_value(clap_id id, double value) {
    const ParamDef &def = DEFS[id];
    value = clamp(value, def.min, def.max);
    return def.curve == STEPPED ? std::round(value) : value;
}

static int stepped(const Glitchfest *glitchfest, clap_id id) {return static_cast<int>(std::lround(glitchfest->values[id]));}

static double next_random(Glitchfest *glitchfest) {
    uint32_t x = glitchfest->random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    glitchfest->random = x;
    return static_cast<double>(x) / 4294967296.0;
}

static uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// ---- patterns: validated on the main thread, read on the audio thread (a torn row only misses a block once) ----

static bool valid_row(const Block *blocks, uint32_t count) {
    if (count > COLUMNS) {return false;}
    uint32_t end = 0;
    for (uint32_t index = 0; index < count; index++) {
        const Block &block = blocks[index];
        if (block.length == 0 || block.start < end || block.start + block.length > COLUMNS) {return false;}
        if (block.chance > 100 || block.ramp > 100) {return false;}
        end = block.start + block.length;
    }
    return true;
}

// every row exactly once
static bool valid_order(const uint8_t *order) {
    bool seen[ROWS] = {};
    for (int position = 0; position < ROWS; position++) {
        if (order[position] >= ROWS || seen[order[position]]) {return false;}
        seen[order[position]] = true;
    }
    return true;
}

static void default_order(uint8_t *order) {for (int position = 0; position < ROWS; position++) {order[position] = static_cast<uint8_t>(position);}}

static void store_row(Glitchfest *glitchfest, uint32_t pattern, uint32_t row, const Block *blocks, uint32_t count) {
    Row &target = glitchfest->patterns[pattern].rows[row];
    target.count = 0;
    std::memcpy(target.blocks, blocks, count * sizeof(Block));
    target.count = count;
    glitchfest->generation.fetch_add(1);
}

// ---- the history every time effect reads from, written before it is read ----

static float history_at(const Glitchfest *glitchfest, int channel, int64_t index) {
    const int64_t oldest = static_cast<int64_t>(glitchfest->clock) - static_cast<int64_t>(glitchfest->history_capacity) + 1;
    if (index > static_cast<int64_t>(glitchfest->clock)) {index = static_cast<int64_t>(glitchfest->clock);}
    if (index < oldest || index < 0) {return 0.0f;}
    return glitchfest->history[channel][static_cast<uint64_t>(index) % glitchfest->history_capacity];
}

// a fractional position in absolute frames, never later than now
static void read_history(const Glitchfest *glitchfest, double position, double &left, double &right) {
    const double now = static_cast<double>(glitchfest->clock);
    if (position > now) {position = now;}
    const double floor = std::floor(position);
    const int64_t index = static_cast<int64_t>(floor);
    const double fraction = position - floor;
    for (int channel = 0; channel < 2; channel++) {
        const double y0 = history_at(glitchfest, channel, index - 1), y1 = history_at(glitchfest, channel, index);
        const double y2 = history_at(glitchfest, channel, index + 1), y3 = history_at(glitchfest, channel, index + 2);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        const double value = ((c3 * fraction + c2) * fraction + c1) * fraction + y1;
        (channel == 0 ? left : right) = value;
    }
}

static double edge(double position, double length, double fade) {
    fade = std::fmin(fade, length * 0.25);
    if (fade <= 0.0) {return 1.0;}
    return std::fmin(1.0, std::fmin(position / fade, (length - position) / fade));
}

static void free_buffers(Glitchfest *glitchfest) {
    for (int channel = 0; channel < 2; channel++) {
        std::free(glitchfest->history[channel]);
        std::free(glitchfest->throw_line[channel]);
        glitchfest->history[channel] = nullptr;
        glitchfest->throw_line[channel] = nullptr;
    }
}

static void clear_lanes(Glitchfest *glitchfest) {
    for (Lane &lane: glitchfest->lanes) {
        std::memset(&lane, 0, sizeof(Lane));
        lane.key = -1;
        lane.gate = 1.0;
    }
    for (double &fade: glitchfest->fade) {fade = 0.0;}
    glitchfest->column_index = -1;
}

// ---- the lanes: which block of which pattern each row is in ----

static double column_beats(const Glitchfest *glitchfest) {
    static const double BARS[] = {1.0, 2.0, 4.0};
    return BARS[stepped(glitchfest, P_LENGTH)] * 4.0 / COLUMNS;
}

static void locate(Glitchfest *glitchfest, int64_t column_index) {
    glitchfest->column_index = column_index;
    const int64_t cycle = column_index / COLUMNS;
    const int column = static_cast<int>(column_index % COLUMNS);
    glitchfest->slot = static_cast<int>(cycle % SLOTS);
    const int pattern = glitchfest->sequence[glitchfest->slot];
    const double width = column_beats(glitchfest);
    for (int row = 0; row < ROWS; row++) {
        Lane &lane = glitchfest->lanes[row];
        const Block *found = nullptr;
        if (pattern >= 0 && pattern < PATTERNS) {
            const Row &source = glitchfest->patterns[pattern].rows[row];
            const uint32_t count = source.count < COLUMNS ? source.count : COLUMNS;
            for (uint32_t index = 0; index < count; index++) {
                const Block &block = source.blocks[index];
                if (column >= block.start && column < block.start + block.length) {
                    found = &block;
                    break;
                }
            }
        }
        if (found == nullptr) {
            lane.key = -1;
            lane.armed = false;
            continue;
        }
        const int64_t key = (cycle * PATTERNS + pattern) * COLUMNS + found->start;
        if (key == lane.key) {continue;}
        if (row <= E_PITCH && lane.amount > 1e-6) {
            glitchfest->fading[row] = lane;
            glitchfest->fade[row] = FADE_SECONDS * glitchfest->sample_rate;
        }
        lane.key = key;
        lane.armed = next_random(glitchfest) * 100.0 < found->chance;
        lane.start_beat = static_cast<double>(cycle * COLUMNS + found->start) * width;
        lane.length_beats = found->length * width;
        lane.ramp = found->ramp / 100.0;
        lane.phase = 0.0;
        lane.grain_index[0] = lane.grain_index[1] = -1;
    }
}

static void forget_blocks(Glitchfest *glitchfest) {
    for (Lane &lane: glitchfest->lanes) {lane.key = -1;}
    glitchfest->column_index = -1;
}

// ---- the effects, top to bottom; a time effect replaces the signal with what it reads from the history ----

static void blend(double &left, double &right, double wet_left, double wet_right, double amount) {
    left += (wet_left - left) * amount;
    right += (wet_right - right) * amount;
}

// what a time effect plays for one lane, so a block that starts while the last one sounds can fade from it
static void time_effect(Glitchfest *glitchfest, int row, Lane &lane, double beat, double frames_per_beat, double &wet_left,
                        double &wet_right) {
    const double rate = glitchfest->sample_rate;
    const double now = static_cast<double>(glitchfest->clock);
    const double *values = glitchfest->values;
    const double elapsed = std::fmax(0.0, (beat - lane.start_beat) * frames_per_beat);
    const double length = std::fmax(1.0, lane.length_beats * frames_per_beat);
    const double progress = clamp(elapsed / length, 0.0, 1.0);
    const double start = now - elapsed;
    switch (row) {
        case E_STUTTER: {
            const double slice = std::fmax(16.0, STUTTER_BEATS[stepped(glitchfest, P_STUTTER_DIV)] * frames_per_beat);
            const double repeat = std::floor(elapsed / slice), offset = elapsed - repeat * slice;
            read_history(glitchfest, start + offset, wet_left, wet_right);
            const double gain = std::pow(1.0 - values[P_STUTTER_DECAY], repeat) * edge(offset, slice, 0.0015 * rate);
            wet_left *= gain;
            wet_right *= gain;
            break;
        }
        case E_SHUFFLE: {
            const double slice = std::fmax(16.0, SLICE_BEATS[stepped(glitchfest, P_SHUFFLE_SLICE)] * frames_per_beat);
            const double bar = 4.0 * frames_per_beat;
            const uint32_t slices = static_cast<uint32_t>(std::fmax(1.0, std::floor(bar / slice)));
            const double index = std::floor(elapsed / slice), offset = elapsed - index * slice;
            const uint32_t seed = static_cast<uint32_t>(stepped(glitchfest, P_SHUFFLE_SEED)) * 2654435761u;
            const uint32_t pick = hash(seed ^ hash(static_cast<uint32_t>(index) * 40503u ^ static_cast<uint32_t>(lane.key))) % slices;
            read_history(glitchfest, start - bar + pick * slice + offset, wet_left, wet_right);
            const double gain = edge(offset, slice, 0.0015 * rate);
            wet_left *= gain;
            wet_right *= gain;
            break;
        }
        case E_REVERSE: {
            const double size = std::fmax(16.0, REVERSE_BEATS[stepped(glitchfest, P_REVERSE_SIZE)] * frames_per_beat);
            const double index = std::floor(elapsed / size), offset = elapsed - index * size;
            read_history(glitchfest, start + index * size - offset, wet_left, wet_right);
            const double gain = edge(offset, size, values[P_REVERSE_FADE] * 0.001 * rate);
            wet_left *= gain;
            wet_right *= gain;
            break;
        }
        case E_SCRATCH: {
            const double depth = values[P_SCRATCH_DEPTH] * 0.5 * frames_per_beat;
            const double phase = elapsed / frames_per_beat * values[P_SCRATCH_RATE];
            read_history(glitchfest, start - depth * (1.0 - std::cos(TAU * phase)) * 0.5, wet_left, wet_right);
                            break;
        }
        case E_FREEZE: {
            const double size = std::fmax(16.0, values[P_FREEZE_SIZE] * 0.001 * rate);
            double sum_left = 0.0, sum_right = 0.0;
            for (int grain = 0; grain < 2; grain++) {
                const double position = elapsed + grain * size * 0.5;
                const int64_t index = static_cast<int64_t>(std::floor(position / size));
                const double offset = position - static_cast<double>(index) * size;
                if (index != lane.grain_index[grain]) {
                    lane.grain_index[grain] = index;
                    lane.grain_offset[grain] = next_random(glitchfest) * values[P_FREEZE_JITTER] * size * 4.0;
                }
                double grain_left, grain_right;
                read_history(glitchfest, start - size - lane.grain_offset[grain] + offset, grain_left, grain_right);
                const double window = std::sin(M_PI * offset / size);
                sum_left += grain_left * window * window;
                sum_right += grain_right * window * window;
            }
            wet_left = sum_left;
            wet_right = sum_right;
            break;
        }
        case E_TAPE: {
            const double power = 0.3 + values[P_TAPE_CURVE] * 3.0;
            const bool stop = stepped(glitchfest, P_TAPE_MODE) == 0;
            const double travel = stop ? length * (1.0 - std::pow(1.0 - progress, power + 1.0)) / (power + 1.0)
                : length * std::pow(progress, power + 1.0) / (power + 1.0);
            const double speed = std::pow(stop ? 1.0 - progress : progress, power);
            const double gain = std::fmin(1.0, std::fmin(speed * 20.0, (stop ? 1.0 - progress : progress) * 40.0));
            read_history(glitchfest, start + travel, wet_left, wet_right);
            wet_left *= gain;
            wet_right *= gain;
            break;
        }
        case E_PITCH: {
            const double glide = values[P_PITCH_GLIDE];
            const double semitones = values[P_PITCH_SEMI] * (glide > 0.0 ? std::fmin(1.0, progress / glide) : 1.0);
            const double ratio = std::pow(2.0, semitones / 12.0);
            const double window = 0.07 * rate;
            lane.phase += 1.0 - ratio;
            lane.phase -= std::floor(lane.phase / window) * window;
            double sum_left = 0.0, sum_right = 0.0;
            for (int tap = 0; tap < 2; tap++) {
                const double delay = std::fmod(lane.phase + tap * window * 0.5, window);
                double tap_left, tap_right;
                read_history(glitchfest, now - delay, tap_left, tap_right);
                const double shape = std::sin(M_PI * delay / window);
                sum_left += tap_left * shape * shape;
                sum_right += tap_right * shape * shape;
            }
            wet_left = sum_left;
            wet_right = sum_right;
            break;
        }
        default:
            break;
    }
}

// the echo keeps sounding after its block, only the send follows the block
static void throw_effect(Glitchfest *glitchfest, double send, double frames_per_beat, double &left, double &right) {
    const uint32_t capacity = glitchfest->throw_capacity;
    const double delay = clamp(THROW_BEATS[stepped(glitchfest, P_THROW_TIME)] * frames_per_beat, 1.0, capacity - 2.0);
    const double read = static_cast<double>(glitchfest->throw_write) + capacity - delay;
    const uint32_t index = static_cast<uint32_t>(read);
    const double fraction = read - index;
    const double feedback = glitchfest->values[P_THROW_FEEDBACK];
    double echo[2];
    for (int channel = 0; channel < 2; channel++) {
        const float *line = glitchfest->throw_line[channel];
        echo[channel] = line[index % capacity] + (line[(index + 1) % capacity] - line[index % capacity]) * fraction;
        glitchfest->throw_damp[channel] += (echo[channel] - glitchfest->throw_damp[channel]) * 0.45;
    }
    const double input[2] = {left * send, right * send};
    for (int channel = 0; channel < 2; channel++) {
        glitchfest->throw_line[channel][glitchfest->throw_write] = static_cast<float>(std::tanh(input[channel] + glitchfest->throw_damp[channel] * feedback));
    }
    glitchfest->throw_write = (glitchfest->throw_write + 1) % capacity;
    left += echo[0];
    right += echo[1];
}

static void run_effects(Glitchfest *glitchfest, double beat, double frames_per_beat, double &left, double &right) {
    const double rate = glitchfest->sample_rate;
    const double *values = glitchfest->values;
    for (int position = 0; position < ROWS; position++) {
        const int row = glitchfest->order[position] < ROWS ? glitchfest->order[position] : position;
        Lane &lane = glitchfest->lanes[row];
        const double amount = lane.amount;
        if (row == E_THROW) {
            throw_effect(glitchfest, amount, frames_per_beat, left, right);
            continue;
        }
        if (amount < 1e-6) {
            if (row == E_FILTER) {lane.ic1[0] = lane.ic1[1] = lane.ic2[0] = lane.ic2[1] = 0.0;}
            if (row == E_GATE) {lane.gate = 1.0;}
            continue;
        }
        const double elapsed = std::fmax(0.0, (beat - lane.start_beat) * frames_per_beat);
        const double progress = clamp(elapsed / std::fmax(1.0, lane.length_beats * frames_per_beat), 0.0, 1.0);
        if (row <= E_PITCH) {
            double wet_left, wet_right;
            time_effect(glitchfest, row, lane, beat, frames_per_beat, wet_left, wet_right);
            if (glitchfest->fade[row] > 0.0) {
                double old_left, old_right;
                time_effect(glitchfest, row, glitchfest->fading[row], beat, frames_per_beat, old_left, old_right);
                const double weight = glitchfest->fade[row] / (FADE_SECONDS * rate);
                wet_left += (old_left - wet_left) * weight;
                wet_right += (old_right - wet_right) * weight;
                glitchfest->fade[row] -= 1.0;
            }
            blend(left, right, wet_left, wet_right, amount);
            continue;
        }
        switch (row) {
            case E_CRUSH: {
                const double steps = std::pow(2.0, values[P_CRUSH_BITS] - 1.0);
                if (lane.phase <= 0.0) {lane.phase = 1.0;}
                if (lane.phase >= 1.0) {
                    lane.phase -= std::floor(lane.phase);
                    lane.hold[0] = std::round(left * steps) / steps;
                    lane.hold[1] = std::round(right * steps) / steps;
                    if (lane.phase <= 0.0) {lane.phase = 1e-9;}
                }
                lane.phase += values[P_CRUSH_RATE] / rate;
                blend(left, right, lane.hold[0], lane.hold[1], amount);
                break;
            }
            case E_FILTER: {
                const double cutoff = stepped(glitchfest, P_FILTER_DIR) == 0 ? 18000.0 * std::pow(200.0 / 18000.0, progress)
                    : 200.0 * std::pow(90.0, progress);
                const double g = std::tan(M_PI * std::fmin(cutoff, 0.45 * rate) / rate);
                const double k = 2.0 - 1.8 * values[P_FILTER_RESO];
                const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                double output[2];
                const double input[2] = {left, right};
                for (int channel = 0; channel < 2; channel++) {
                    const double v3 = input[channel] - lane.ic2[channel];
                    const double v1 = a1 * lane.ic1[channel] + a2 * v3;
                    const double v2 = lane.ic2[channel] + a2 * lane.ic1[channel] + a3 * v3;
                    lane.ic1[channel] = 2.0 * v1 - lane.ic1[channel];
                    lane.ic2[channel] = 2.0 * v2 - lane.ic2[channel];
                    output[channel] = v2;
                }
                blend(left, right, output[0], output[1], amount);
                break;
            }
            case E_GATE: {
                const double period = std::fmax(16.0, GATE_BEATS[stepped(glitchfest, P_GATE_DIV)] * frames_per_beat);
                const double phase = std::fmod(elapsed, period) / period;
                const double target = phase < values[P_GATE_DUTY] ? 1.0 : 0.0;
                lane.gate += (target - lane.gate) * (1.0 - std::exp(-1.0 / (0.001 * rate)));
                const double gain = 1.0 - amount * (1.0 - lane.gate);
                left *= gain;
                right *= gain;
                break;
            }
            case E_PAN: {
                const double period = PAN_BEATS[stepped(glitchfest, P_PAN_RATE)] * frames_per_beat;
                const double pan = values[P_PAN_WIDTH] * std::sin(TAU * elapsed / period);
                const double angle = (pan + 1.0) * M_PI * 0.25;
                left *= 1.0 + amount * (std::cos(angle) * M_SQRT2 - 1.0);
                right *= 1.0 + amount * (std::sin(angle) * M_SQRT2 - 1.0);
                break;
            }
            default:
                break;
        }
    }
}

// one frame: history in, the lanes on the transport, the effects, the mix
static void render_frame(Glitchfest *glitchfest, double beat, double frames_per_beat, double declick, float in_left, float in_right,
                         float &out_left, float &out_right) {
    const uint32_t write = static_cast<uint32_t>(glitchfest->clock % glitchfest->history_capacity);
    glitchfest->history[0][write] = in_left;
    glitchfest->history[1][write] = in_right;
    if (glitchfest->playing) {
        if (beat < glitchfest->last_beat - 1e-9) {forget_blocks(glitchfest);}
        const double width = column_beats(glitchfest);
        const int64_t index = static_cast<int64_t>(std::floor(std::fmax(0.0, beat) / width));
        const uint32_t generation = glitchfest->generation.load();
        if (index != glitchfest->column_index || generation != glitchfest->seen_generation) {
            glitchfest->seen_generation = generation;
            locate(glitchfest, index);
        }
        glitchfest->column = std::fmod(std::fmax(0.0, beat) / width, static_cast<double>(COLUMNS));
    } else if (glitchfest->column_index >= 0) {
        forget_blocks(glitchfest);
    }
    glitchfest->last_beat = beat;
    for (Lane &lane: glitchfest->lanes) {
        double target = 0.0;
        if (glitchfest->playing && lane.key >= 0 && lane.armed) {
            const double progress = clamp((beat - lane.start_beat) / lane.length_beats, 0.0, 1.0);
            target = lane.ramp > 0.0 ? std::fmin(1.0, std::fmin(progress, 1.0 - progress) / (lane.ramp * 0.5)) : 1.0;
        }
        lane.amount = std::fabs(target - lane.amount) < 1e-5 ? target : lane.amount + (target - lane.amount) * declick;
    }
    double left = in_left, right = in_right;
    run_effects(glitchfest, beat, frames_per_beat, left, right);
    const double mix = glitchfest->values[P_MIX];
    out_left = static_cast<float>(in_left + (left - in_left) * mix);
    out_right = static_cast<float>(in_right + (right - in_right) * mix);
    glitchfest->clock++;
}

// ---- events ----

static void set_value(Glitchfest *glitchfest, clap_id id, double value) {
    if (id >= PARAM_COUNT) {return;}
    glitchfest->values[id] = clamp_value(id, value);
}

static void handle_event(Glitchfest *glitchfest, const clap_event_header_t *header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) {return;}
    if (header->type == CLAP_EVENT_PARAM_VALUE) {
        const clap_event_param_value_t *param = reinterpret_cast<const clap_event_param_value_t *>(header);
        set_value(glitchfest, param->param_id, param->value);
    }
}

static void push_out_event(Glitchfest *glitchfest, uint16_t type, clap_id id, double value) {
    if (glitchfest->out_write - glitchfest->out_read >= OUT_EVENTS) {return;}
    glitchfest->out_events[glitchfest->out_write % OUT_EVENTS] = OutEvent{type, id, value};
    glitchfest->out_write++;
}

static void drain_out_events(Glitchfest *glitchfest, const clap_output_events_t *out) {
    while (glitchfest->out_read != glitchfest->out_write) {
        const OutEvent &event = glitchfest->out_events[glitchfest->out_read % OUT_EVENTS];
        if (event.type == CLAP_EVENT_PARAM_VALUE) {
            clap_event_param_value_t value = {};
            value.header = {sizeof(value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            value.param_id = event.id;
            value.note_id = -1;
            value.port_index = -1;
            value.channel = -1;
            value.key = -1;
            value.value = event.value;
            out->try_push(out, &value.header);
        } else {
            clap_event_param_gesture_t gesture = {};
            gesture.header = {sizeof(gesture), 0, CLAP_CORE_EVENT_SPACE_ID, event.type, 0};
            gesture.param_id = event.id;
            out->try_push(out, &gesture.header);
        }
        glitchfest->out_read++;
    }
}

// ---- process ----

static clap_process_status glitchfest_process(const clap_plugin_t *plugin, const clap_process_t *process) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1) {return CLAP_PROCESS_ERROR;}
    const clap_audio_buffer_t &input = process->audio_inputs[0];
    const clap_audio_buffer_t &output = process->audio_outputs[0];
    if (input.channel_count < 1 || output.channel_count < 2) {return CLAP_PROCESS_ERROR;}
    const float *in_left = input.data32[0];
    const float *in_right = input.channel_count > 1 ? input.data32[1] : input.data32[0];
    float *out_left = output.data32[0];
    float *out_right = output.data32[1];
    const uint32_t frames = process->frames_count;
    const clap_event_transport_t *transport = process->transport;
    double beat = glitchfest->free_beats;
    glitchfest->playing = true;
    if (transport != nullptr) {
        if (transport->flags & CLAP_TRANSPORT_HAS_TEMPO) {glitchfest->tempo = clamp(transport->tempo, 20.0, 999.0);}
        glitchfest->playing = (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        if (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {beat = static_cast<double>(transport->song_pos_beats) / CLAP_BEATTIME_FACTOR;}
    }
    const double frames_per_beat = glitchfest->sample_rate * 60.0 / glitchfest->tempo;
    const double declick = 1.0 - std::exp(-1.0 / (0.003 * glitchfest->sample_rate));
    const uint32_t count = process->in_events->size(process->in_events);
    uint32_t next = 0;
    for (uint32_t frame = 0; frame < frames; frame++) {
        while (next < count) {
            const clap_event_header_t *header = process->in_events->get(process->in_events, next);
            if (header->time > frame) {break;}
            handle_event(glitchfest, header);
            next++;
        }
        const float left = in_left[frame], right = in_right[frame];
        if (glitchfest->history[0] == nullptr) {
            out_left[frame] = left;
            out_right[frame] = right;
            continue;
        }
        render_frame(glitchfest, beat + frame / frames_per_beat, frames_per_beat, declick, left, right, out_left[frame], out_right[frame]);
    }
    for (; next < count; next++) {handle_event(glitchfest, process->in_events->get(process->in_events, next));}
    glitchfest->free_beats = beat + (glitchfest->playing ? frames / frames_per_beat : 0.0);
    drain_out_events(glitchfest, process->out_events);
    return CLAP_PROCESS_CONTINUE;
}

// ---- plugin ----

static bool glitchfest_init(const clap_plugin_t *plugin) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    const clap_host_t *host = glitchfest->host;
    glitchfest->host_params = static_cast<const clap_host_params_t *>(host->get_extension(host, CLAP_EXT_PARAMS));
    glitchfest->host_state = static_cast<const clap_host_state_t *>(host->get_extension(host, CLAP_EXT_STATE));
    glitchfest->host_webview = static_cast<const clap_host_webview_t *>(host->get_extension(host, CLAP_EXT_WEBVIEW));
    glitchfest->host_hovered = static_cast<const clap_host_param_hovered_t *>(host->get_extension(host, CLAP_EXT_PARAM_HOVERED));
    return true;
}

static void glitchfest_destroy(const clap_plugin_t *plugin) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    free_buffers(glitchfest);
    glitchfest->~Glitchfest();
    std::free(glitchfest);
}

static void glitchfest_reset(const clap_plugin_t *plugin) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    clear_lanes(glitchfest);
    for (int channel = 0; channel < 2; channel++) {
        if (glitchfest->history[channel] != nullptr) {std::memset(glitchfest->history[channel], 0, glitchfest->history_capacity * sizeof(float));}
        if (glitchfest->throw_line[channel] != nullptr) {std::memset(glitchfest->throw_line[channel], 0, glitchfest->throw_capacity * sizeof(float));}
        glitchfest->throw_damp[channel] = 0.0;
    }
    glitchfest->clock = 0;
    glitchfest->last_beat = 0.0;
}

static bool glitchfest_activate(const clap_plugin_t *plugin, double sample_rate, uint32_t min_frames, uint32_t max_frames) {
    (void) min_frames; (void) max_frames;
    Glitchfest *glitchfest = glitchfest_of(plugin);
    free_buffers(glitchfest);
    glitchfest->sample_rate = sample_rate;
    glitchfest->history_capacity = static_cast<uint32_t>(std::ceil(sample_rate * HISTORY_SECONDS));
    glitchfest->throw_capacity = static_cast<uint32_t>(std::ceil(sample_rate * THROW_SECONDS));
    glitchfest->throw_write = 0;
    for (int channel = 0; channel < 2; channel++) {
        glitchfest->history[channel] = static_cast<float *>(std::calloc(glitchfest->history_capacity, sizeof(float)));
        glitchfest->throw_line[channel] = static_cast<float *>(std::calloc(glitchfest->throw_capacity, sizeof(float)));
        if (glitchfest->history[channel] == nullptr || glitchfest->throw_line[channel] == nullptr) {
            free_buffers(glitchfest);
            return false;
        }
    }
    glitchfest_reset(plugin);
    return true;
}

static void glitchfest_deactivate(const clap_plugin_t *plugin) {(void) plugin;}

static bool glitchfest_start_processing(const clap_plugin_t *plugin) {(void) plugin; return true;}

static void glitchfest_stop_processing(const clap_plugin_t *plugin) {(void) plugin;}

static uint32_t audio_ports_count(const clap_plugin_t *plugin, bool is_input) {(void) plugin; (void) is_input; return 1;}

static bool audio_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input, clap_audio_port_info_t *info) {
    (void) plugin;
    if (index > 0) {return false;}
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", is_input ? "Stereo In" : "Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t audio_ports = {audio_ports_count, audio_ports_get};

static uint32_t params_count(const clap_plugin_t *plugin) {(void) plugin; return PARAM_COUNT;}

static bool params_get_info(const clap_plugin_t *plugin, uint32_t index, clap_param_info_t *info) {
    (void) plugin;
    if (index >= PARAM_COUNT) {return false;}
    const ParamDef &def = DEFS[index];
    std::memset(info, 0, sizeof(*info));
    info->id = index;
    info->flags = index == P_DICE ? 0 : CLAP_PARAM_IS_AUTOMATABLE;
    if (def.curve == STEPPED) {info->flags |= CLAP_PARAM_IS_STEPPED;}
    if (def.texts != nullptr) {info->flags |= CLAP_PARAM_IS_ENUM;}
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.value;
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "%s", def.module);
    return true;
}

static bool params_get_value(const clap_plugin_t *plugin, clap_id id, double *value) {
    if (id >= PARAM_COUNT) {return false;}
    *value = glitchfest_of(plugin)->values[id];
    return true;
}

static bool params_value_to_text(const clap_plugin_t *plugin, clap_id id, double value, char *text, uint32_t size) {
    (void) plugin;
    if (id >= PARAM_COUNT || size == 0) {return false;}
    const ParamDef &def = DEFS[id];
    value = clamp_value(id, value);
    switch (def.unit) {
        case U_CHOICE: std::snprintf(text, size, "%s", def.texts[static_cast<int>(value)]); break;
        case U_PERCENT: std::snprintf(text, size, "%.0f %%", value * 100.0); break;
        case U_MS: std::snprintf(text, size, value < 10.0 ? "%.1f ms" : "%.0f ms", value); break;
        case U_HZ: std::snprintf(text, size, value < 1000.0 ? "%.0f Hz" : "%.1f kHz", value < 1000.0 ? value : value * 0.001); break;
        case U_TIMES: std::snprintf(text, size, "%.2f x", value); break;
        case U_SEMITONES: std::snprintf(text, size, "%+.0f st", value); break;
        case U_BITS: std::snprintf(text, size, "%.0f bit", value); break;
        default: std::snprintf(text, size, "%.0f", value); break;
    }
    return true;
}

static bool matches(const char *text, const char *label) {
    while (*text == ' ') {text++;}
    for (; *label != '\0'; text++, label++) {
        const char letter = *text >= 'A' && *text <= 'Z' ? static_cast<char>(*text + 32) : *text;
        const char expected = *label >= 'A' && *label <= 'Z' ? static_cast<char>(*label + 32) : *label;
        if (letter != expected) {return false;}
    }
    return *text == '\0' || *text == ' ';
}

static bool params_text_to_value(const clap_plugin_t *plugin, clap_id id, const char *text, double *value) {
    (void) plugin;
    if (id >= PARAM_COUNT || text == nullptr) {return false;}
    const ParamDef &def = DEFS[id];
    if (def.texts != nullptr) {
        for (int index = 0; index <= static_cast<int>(def.max); index++) {
            if (matches(text, def.texts[index])) {
                *value = index;
                return true;
            }
        }
        return false;
    }
    char *end = nullptr;
    double number = std::strtod(text, &end);
    if (end == text) {return false;}
    while (*end == ' ') {end++;}
    if (def.unit == U_PERCENT) {number *= 0.01;}
    if (def.unit == U_HZ && (*end == 'k' || *end == 'K')) {number *= 1000.0;}
    *value = clamp_value(id, number);
    return true;
}

static void params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    const uint32_t count = in->size(in);
    for (uint32_t index = 0; index < count; index++) {handle_event(glitchfest, in->get(in, index));}
    drain_out_events(glitchfest, out);
}

static const clap_plugin_params_t params = {
    params_count, params_get_info, params_get_value, params_value_to_text, params_text_to_value, params_flush
};

// ---- state: magic, version, sizes, the parameters, the sequence, every row as a count and its blocks, the order ----

static bool write_all(const clap_ostream_t *stream, const void *data, uint64_t size) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint64_t written = 0;
    while (written < size) {
        const int64_t result = stream->write(stream, bytes + written, size - written);
        if (result <= 0) {return false;}
        written += result;
    }
    return true;
}

static bool read_all(const clap_istream_t *stream, void *data, uint64_t size) {
    uint8_t *bytes = static_cast<uint8_t *>(data);
    uint64_t read = 0;
    while (read < size) {
        const int64_t result = stream->read(stream, bytes + read, size - read);
        if (result <= 0) {return false;}
        read += result;
    }
    return true;
}

static bool state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    const uint32_t header[7] = {STATE_MAGIC, STATE_VERSION, PARAM_COUNT, ROWS, PATTERNS, COLUMNS, SLOTS};
    if (!write_all(stream, header, sizeof(header))) {return false;}
    if (!write_all(stream, glitchfest->values, sizeof(glitchfest->values))) {return false;}
    if (!write_all(stream, glitchfest->sequence, sizeof(glitchfest->sequence))) {return false;}
    for (const Pattern &pattern: glitchfest->patterns) {
        for (const Row &row: pattern.rows) {
            const uint8_t count = static_cast<uint8_t>(row.count);
            if (!write_all(stream, &count, 1) || !write_all(stream, row.blocks, count * sizeof(Block))) {return false;}
        }
    }
    return write_all(stream, glitchfest->order, ROWS);
}

static bool state_load(const clap_plugin_t *plugin, const clap_istream_t *stream) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    uint32_t header[7];
    if (!read_all(stream, header, sizeof(header)) || header[0] != STATE_MAGIC || header[1] < 1 || header[1] > STATE_VERSION) {return false;}
    if (header[3] != ROWS || header[4] != PATTERNS || header[5] != COLUMNS || header[6] != SLOTS) {return false;}
    const uint32_t param_count = header[2];
    double values[PARAM_COUNT];
    for (clap_id id = 0; id < PARAM_COUNT; id++) {values[id] = glitchfest->values[id];}
    for (uint32_t id = 0; id < param_count; id++) {
        double value;
        if (!read_all(stream, &value, 8)) {return false;}
        if (id < PARAM_COUNT) {values[id] = clamp_value(id, value);}
    }
    int8_t sequence[SLOTS];
    if (!read_all(stream, sequence, sizeof(sequence))) {return false;}
    for (int8_t &slot: sequence) {if (slot < -1 || slot >= PATTERNS) {return false;}}
    static Pattern patterns[PATTERNS];
    for (Pattern &pattern: patterns) {
        for (Row &row: pattern.rows) {
            uint8_t count;
            if (!read_all(stream, &count, 1) || count > COLUMNS) {return false;}
            if (!read_all(stream, row.blocks, count * sizeof(Block)) || !valid_row(row.blocks, count)) {return false;}
            row.count = count;
        }
    }
    uint8_t order[ROWS];
    default_order(order);
    if (header[1] >= 2 && (!read_all(stream, order, ROWS) || !valid_order(order))) {return false;}
    std::memcpy(glitchfest->values, values, sizeof(values));
    std::memcpy(glitchfest->sequence, sequence, sizeof(sequence));
    std::memcpy(glitchfest->order, order, sizeof(order));
    for (uint32_t pattern = 0; pattern < PATTERNS; pattern++) {
        for (uint32_t row = 0; row < ROWS; row++) {
            const Row &source = patterns[pattern].rows[row];
            store_row(glitchfest, pattern, row, source.blocks, source.count);
        }
    }
    return true;
}

static const clap_plugin_state_t state = {state_save, state_load};

// ---- webview ----

static int32_t webview_get_uri(const clap_plugin_t *plugin, char *uri, uint32_t capacity) {
    (void) plugin;
    static const char page[] = "/index.html";
    if (capacity > 0 && uri != nullptr) {std::snprintf(uri, capacity, "%s", page);}
    return static_cast<int32_t>(sizeof(page));
}

static bool webview_get_resource(const clap_plugin_t *plugin, const char *path, char *mime, uint32_t capacity,
                                 const clap_ostream_t *stream) {
    (void) plugin; (void) path; (void) mime; (void) capacity; (void) stream;
    return false;
}

static void send_status(Glitchfest *glitchfest) {
    uint8_t message[STATUS_HEADER + PARAM_COUNT * 8];
    std::memset(message, 0, STATUS_HEADER);
    message[0] = PLUGIN_STATUS;
    const uint32_t flags = glitchfest->playing && glitchfest->column_index >= 0 ? 1u : 0u;
    const int32_t slot = glitchfest->slot;
    const uint32_t generation = glitchfest->generation.load();
    const float column = static_cast<float>(glitchfest->column);
    std::memcpy(message + 4, &flags, 4);
    std::memcpy(message + 8, &slot, 4);
    std::memcpy(message + 12, &generation, 4);
    std::memcpy(message + 16, &column, 4);
    std::memcpy(message + STATUS_HEADER, glitchfest->values, sizeof(glitchfest->values));
    glitchfest->host_webview->send(glitchfest->host, message, sizeof(message));
}

static void send_patterns(Glitchfest *glitchfest) {
    static uint8_t message[PATTERNS_HEADER + PATTERNS * ROWS * ROW_BYTES];
    std::memset(message, 0, sizeof(message));
    message[0] = PLUGIN_PATTERNS;
    const uint32_t generation = glitchfest->generation.load();
    std::memcpy(message + 4, &generation, 4);
    std::memcpy(message + 8, glitchfest->sequence, SLOTS);
    std::memcpy(message + 16, glitchfest->order, ROWS);
    uint8_t *cursor = message + PATTERNS_HEADER;
    for (const Pattern &pattern: glitchfest->patterns) {
        for (const Row &row: pattern.rows) {
            const uint32_t count = row.count < COLUMNS ? row.count : COLUMNS;
            std::memcpy(cursor, &count, 4);
            std::memcpy(cursor + 4, row.blocks, count * sizeof(Block));
            cursor += ROW_BYTES;
        }
    }
    glitchfest->host_webview->send(glitchfest->host, message, sizeof(message));
}

static void mark_dirty(Glitchfest *glitchfest) {if (glitchfest->host_state != nullptr) {glitchfest->host_state->mark_dirty(glitchfest->host);}}

static void page_param(Glitchfest *glitchfest, uint16_t type, const uint8_t *bytes, uint32_t size) {
    clap_id id;
    if (size < 8) {return;}
    std::memcpy(&id, bytes + 4, 4);
    if (id >= PARAM_COUNT) {return;}
    if (type == CLAP_EVENT_PARAM_VALUE) {
        double value;
        if (size < 16) {return;}
        std::memcpy(&value, bytes + 8, 8);
        set_value(glitchfest, id, value);
        mark_dirty(glitchfest);
    }
    push_out_event(glitchfest, type, id, glitchfest->values[id]);
    if (glitchfest->host_params != nullptr) {glitchfest->host_params->request_flush(glitchfest->host);}
}

static bool webview_receive(const clap_plugin_t *plugin, const void *buffer, uint32_t size) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
    if (size < 1 || glitchfest->host_webview == nullptr) {return false;}
    switch (bytes[0]) {
        case PAGE_POLL:
            send_status(glitchfest);
            return true;
        case PAGE_PARAM_BEGIN:
            page_param(glitchfest, CLAP_EVENT_PARAM_GESTURE_BEGIN, bytes, size);
            return true;
        case PAGE_PARAM_SET:
            page_param(glitchfest, CLAP_EVENT_PARAM_VALUE, bytes, size);
            return true;
        case PAGE_PARAM_END:
            page_param(glitchfest, CLAP_EVENT_PARAM_GESTURE_END, bytes, size);
            return true;
        case PAGE_HOVER: {
            clap_id id;
            if (size < 8 || glitchfest->host_hovered == nullptr) {return false;}
            std::memcpy(&id, bytes + 4, 4);
            glitchfest->host_hovered->update(glitchfest->host, id < PARAM_COUNT ? id : CLAP_INVALID_ID);
            return true;
        }
        case PAGE_SET_ROW: {
            if (size < 8) {return false;}
            const uint32_t pattern = bytes[4], row = bytes[5], count = bytes[6];
            if (pattern >= PATTERNS || row >= ROWS || count > COLUMNS || size < 8 + count * sizeof(Block)) {return false;}
            Block blocks[COLUMNS];
            std::memcpy(blocks, bytes + 8, count * sizeof(Block));
            if (!valid_row(blocks, count)) {return false;}
            store_row(glitchfest, pattern, row, blocks, count);
            mark_dirty(glitchfest);
            return true;
        }
        case PAGE_SET_SEQUENCE: {
            if (size < 4 + SLOTS) {return false;}
            int8_t sequence[SLOTS];
            std::memcpy(sequence, bytes + 4, SLOTS);
            for (int8_t slot: sequence) {if (slot < -1 || slot >= PATTERNS) {return false;}}
            std::memcpy(glitchfest->sequence, sequence, SLOTS);
            glitchfest->generation.fetch_add(1);
            mark_dirty(glitchfest);
            return true;
        }
        case PAGE_SET_ORDER: {
            if (size < 4 + ROWS) {return false;}
            uint8_t order[ROWS];
            std::memcpy(order, bytes + 4, ROWS);
            if (!valid_order(order)) {return false;}
            std::memcpy(glitchfest->order, order, ROWS);
            glitchfest->generation.fetch_add(1);
            mark_dirty(glitchfest);
            return true;
        }
        case PAGE_REQUEST_PATTERNS:
            send_patterns(glitchfest);
            return true;
        default:
            return false;
    }
}

static const clap_plugin_webview_t webview = {webview_get_uri, webview_get_resource, webview_receive};

// gui: only the webview api, the host owns the window, the panel's 244 x 176 scaled 1 to 3 times
static bool gui_is_api_supported(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    (void) plugin; (void) is_floating;
    return std::strcmp(api, CLAP_WINDOW_API_WEBVIEW) == 0;
}

static bool gui_get_preferred_api(const clap_plugin_t *plugin, const char **api, bool *is_floating) {
    (void) plugin;
    *api = CLAP_WINDOW_API_WEBVIEW;
    *is_floating = false;
    return true;
}

static bool gui_create(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    return gui_is_api_supported(plugin, api, is_floating);
}

static void gui_destroy(const clap_plugin_t *plugin) {(void) plugin;}

static bool gui_set_scale(const clap_plugin_t *plugin, double scale) {(void) plugin; (void) scale; return false;}

static bool gui_get_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) {
    const Glitchfest *glitchfest = glitchfest_of(plugin);
    *width = glitchfest->gui_width;
    *height = glitchfest->gui_height;
    return true;
}

static bool gui_can_resize(const clap_plugin_t *plugin) {(void) plugin; return true;}

static bool gui_get_resize_hints(const clap_plugin_t *plugin, clap_gui_resize_hints_t *hints) {
    (void) plugin;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = true;
    hints->aspect_ratio_width = GUI_WIDTH;
    hints->aspect_ratio_height = GUI_HEIGHT;
    return true;
}

static bool gui_adjust_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) {
    (void) plugin;
    const double wanted = std::fmin(static_cast<double>(*width) / GUI_WIDTH, static_cast<double>(*height) / GUI_HEIGHT);
    const double scale = std::fmin(GUI_MAX_SCALE, std::fmax(GUI_MIN_SCALE, wanted));
    *width = static_cast<uint32_t>(std::lround(GUI_WIDTH * scale));
    *height = static_cast<uint32_t>(std::lround(GUI_HEIGHT * scale));
    return true;
}

static bool gui_set_size(const clap_plugin_t *plugin, uint32_t width, uint32_t height) {
    Glitchfest *glitchfest = glitchfest_of(plugin);
    gui_adjust_size(plugin, &width, &height);
    glitchfest->gui_width = width;
    glitchfest->gui_height = height;
    return true;
}

static bool gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {(void) plugin; (void) window; return true;}

static bool gui_set_transient(const clap_plugin_t *plugin, const clap_window_t *window) {(void) plugin; (void) window; return false;}

static void gui_suggest_title(const clap_plugin_t *plugin, const char *title) {(void) plugin; (void) title;}

static bool gui_show(const clap_plugin_t *plugin) {(void) plugin; return true;}

static bool gui_hide(const clap_plugin_t *plugin) {(void) plugin; return true;}

static const clap_plugin_gui_t gui = {
    gui_is_api_supported, gui_get_preferred_api, gui_create, gui_destroy, gui_set_scale, gui_get_size, gui_can_resize,
    gui_get_resize_hints, gui_adjust_size, gui_set_size, gui_set_parent, gui_set_transient, gui_suggest_title, gui_show,
    gui_hide
};

// device panel: the window's page, 244 wide at the panel's 176
static bool panel_get_width(const clap_plugin_t *plugin, uint32_t height, uint32_t *width) {
    (void) plugin;
    *width = static_cast<uint32_t>(std::lround(static_cast<double>(height) * GUI_WIDTH / GUI_HEIGHT));
    return true;
}

static const opendaw_plugin_device_panel_t device_panel = {webview_get_uri, panel_get_width};

static const void *glitchfest_get_extension(const clap_plugin_t *plugin, const char *id) {
    (void) plugin;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {return &audio_ports;}
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {return &params;}
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {return &state;}
    if (std::strcmp(id, CLAP_EXT_WEBVIEW) == 0) {return &webview;}
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) {return &gui;}
    if (std::strcmp(id, OPENDAW_EXT_DEVICE_PANEL) == 0) {return &device_panel;}
    return nullptr;
}

static void glitchfest_on_main_thread(const clap_plugin_t *plugin) {(void) plugin;}

static const clap_plugin_t *create(const clap_plugin_factory_t *factory, const clap_host_t *host, const char *plugin_id) {
    (void) factory;
    if (std::strcmp(plugin_id, descriptor.id) != 0) {return nullptr;}
    void *memory = std::calloc(1, sizeof(Glitchfest));
    if (memory == nullptr) {return nullptr;}
    Glitchfest *glitchfest = new(memory) Glitchfest();
    glitchfest->host = host;
    for (clap_id id = 0; id < PARAM_COUNT; id++) {glitchfest->values[id] = DEFS[id].value;}
    std::memset(glitchfest->patterns, 0, sizeof(glitchfest->patterns));
    std::memset(glitchfest->sequence, 0, sizeof(glitchfest->sequence));
    default_order(glitchfest->order);
    clear_lanes(glitchfest);
    glitchfest->sample_rate = 48000.0;
    glitchfest->tempo = 120.0;
    glitchfest->random = 0x9e3779b9u;
    glitchfest->gui_width = GUI_WIDTH * 2;
    glitchfest->gui_height = GUI_HEIGHT * 2;
    glitchfest->plugin.desc = &descriptor;
    glitchfest->plugin.plugin_data = glitchfest;
    glitchfest->plugin.init = glitchfest_init;
    glitchfest->plugin.destroy = glitchfest_destroy;
    glitchfest->plugin.activate = glitchfest_activate;
    glitchfest->plugin.deactivate = glitchfest_deactivate;
    glitchfest->plugin.start_processing = glitchfest_start_processing;
    glitchfest->plugin.stop_processing = glitchfest_stop_processing;
    glitchfest->plugin.reset = glitchfest_reset;
    glitchfest->plugin.process = glitchfest_process;
    glitchfest->plugin.get_extension = glitchfest_get_extension;
    glitchfest->plugin.on_main_thread = glitchfest_on_main_thread;
    return &glitchfest->plugin;
}

static uint32_t plugin_count(const clap_plugin_factory_t *factory) {(void) factory; return 1;}

static const clap_plugin_descriptor_t *plugin_descriptor(const clap_plugin_factory_t *factory, uint32_t index) {
    (void) factory;
    return index == 0 ? &descriptor : nullptr;
}

static const clap_plugin_factory_t factory = {plugin_count, plugin_descriptor, create};

static bool entry_init(const char *path) {(void) path; return true;}

static void entry_deinit(void) {}

static const void *entry_get_factory(const char *id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entry_init, entry_deinit, entry_get_factory};

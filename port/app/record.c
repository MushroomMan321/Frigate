// Recordings (--record FILE, port/app/record.h): the buttons, lid and touch the game is given at each VBlank, after the
// window's input (port/app/present.c) or a replay's (port/app/replay.c), written on video frames as an input file that
// --input replays to the same frames (port/tools/oracle/README.md, "Recordings"). The header says what the run started
// from: its clock's effective time and mode (port/hal/rtc) and the seeds given to the game
// (port/hal/gfl/random_seed.h). The lines are formatted by the oracle's input file writer
// (port/tools/oracle/pbwinput.c), which the replays' reader parses, so the two can't drift apart.
#include "app/record.h"
#include "hal/host/host.h"
#include "tools/oracle/pbwinput.h"
#include <errno.h>
#include <string.h>

// Every seed a recording declares fits in a file
_Static_assert(PORT_RECORD_LIVE_SEEDS <= PBW_INPUT_MAX_SEEDS, "a live recording's seeds must fit in a file");

static FILE *sFile;
static const char *sPath;
// The state at power-on, and the last one written: a line is written only when it changes
static PbwInputState sStartState;
static PbwInputState sState;
// Set once a write failed or a sample was refused: the file stops there, and the run must not end as if it were whole
static int sFailed;

int port_record_draw_seeds(uint64_t *seeds, size_t count) {
    if (!port_host_random_bytes(seeds, count * sizeof(*seeds))) {
        fprintf(stderr, "port: --record: the system gave no random bytes for the recording's seeds\n");
        return 0;
    }
    return 1;
}

// A state as a file holds it: the known buttons only, and the pen's place only while it is down
static PbwInputState State(uint32_t buttons, int lidClosed, int touching, int x, int y) {
    PbwInputState state;

    state.buttons = buttons & PBW_INPUT_ALL;
    state.lidClosed = lidClosed != 0;
    state.touching = touching != 0;
    state.touchX = state.touching ? (uint8_t)x : 0;
    state.touchY = state.touching ? (uint8_t)y : 0;
    return state;
}

static int SameState(const PbwInputState *a, const PbwInputState *b) {
    return a->buttons == b->buttons && a->lidClosed == b->lidClosed && a->touching == b->touching &&
           a->touchX == b->touchX && a->touchY == b->touchY;
}

static void Fail(const char *why) {
    fprintf(stderr, "port: --record %s: %s; the recording stops at its last complete line\n", sPath, why);
    sFailed = 1;
}

// Writes text and flushes it, so that the file on disk always ends with a complete line
static int Write(const char *text) {
    if (fputs(text, sFile) < 0 || fflush(sFile) != 0) {
        Fail(strerror(errno));
        return 0;
    }
    return 1;
}

// A frame's line, "FRAME BUTTONS TOUCH", written whole
static int WriteState(uint32_t frame, const PbwInputState *state) {
    char columns[96];
    char line[128];

    if (!pbw_input_format_state(state, columns, sizeof(columns))) {
        Fail("a state that doesn't fit a line");
        return 0;
    }
    snprintf(line, sizeof(line), "%u %s\n", (unsigned)frame, columns);
    return Write(line);
}

// The file's name without its directories, for the header's comment
static const char *BaseName(const char *path) {
    const char *name = path;

    for (const char *at = path; *at != '\0'; at++) {
        if (*at == '/' || *at == '\\') {
            name = at + 1;
        }
    }
    return name;
}

static int WriteHeader(const PortRecordStart *start) {
    char line[256];

    if (!Write("pbwinput 1\n")) {
        return 0;
    }
    if (start->input != NULL) {
        snprintf(line, sizeof(line), "# Recorded by Frigate: %s, replaying %s\n", start->program,
                 BaseName(start->input));
    } else {
        snprintf(line, sizeof(line), "# Recorded by Frigate: %s, from the window's input\n", start->program);
    }
    if (!Write(line) ||
        !Write("# --input replays it on the same ROM, from the same starting saves, on a compatible build\n")) {
        return 0;
    }
    snprintf(line, sizeof(line), "version %s\nframes video\ntime %s\nclock %s\n", start->version, start->time,
             start->pinned ? "pinned" : "running");
    if (!Write(line)) {
        return 0;
    }
    for (size_t i = 0; i < start->seedCount; i++) {
        snprintf(line, sizeof(line), "seed 0x%016llx\n", (unsigned long long)start->seeds[i]);
        if (!Write(line)) {
            return 0;
        }
    }
    return Write("# FRAME BUTTONS [TOUCH]\n");
}

int port_record_open(const char *path, const PortRecordStart *start) {
    if (start->seedCount == 0 || start->seedCount > PBW_INPUT_MAX_SEEDS || !pbw_input_valid_time(start->time)) {
        fprintf(stderr, "port: --record %s: a recording needs 1 to %d seeds and a time from 2000 to 2099\n", path,
                PBW_INPUT_MAX_SEEDS);
        return 0;
    }
    // "x": created here, or not at all, so that no file already there is ever overwritten; "b": lines end in LF on
    // every host, as the run directory's files do
    sFile = fopen(path, "wbx");
    if (sFile == NULL) {
        if (errno == EEXIST) {
            fprintf(stderr, "port: --record %s: the file exists; a recording never overwrites a file\n", path);
        } else {
            fprintf(stderr, "port: --record %s: can't create it: %s\n", path, strerror(errno));
        }
        return 0;
    }
    sPath = path;
    sFailed = 0;
    sStartState = State(0, 0, 0, 0, 0);
    sState = sStartState;
    if (!WriteHeader(start)) {
        fclose(sFile);
        sFile = NULL;
        return 0;
    }
    return 1;
}

int port_record_active(void) {
    return sFile != NULL;
}

int port_record_start(uint32_t buttons, int lidClosed, int touching, int x, int y) {
    PbwInputState state = State(buttons, lidClosed, touching, x, y);

    if (sFile == NULL || sFailed) {
        return 0;
    }
    sStartState = state;
    sState = state;
    // Frame 1's state also holds from power-on, in a file as in a run
    if (!SameState(&state, &(PbwInputState){ 0 })) {
        return WriteState(1, &state);
    }
    return 1;
}

int port_record_vblank(uint32_t videoFrame, uint32_t buttons, int lidClosed, int touching, int x, int y) {
    PbwInputState state = State(buttons, lidClosed, touching, x, y);

    if (sFile == NULL || sFailed) {
        return 0;
    }
    if (videoFrame == 1) {
        // A replay of the file gives frame 1's state from power-on and at VBlank 1 alike, so the two must be one
        if (!SameState(&state, &sStartState)) {
            Fail("the input at the first VBlank isn't the one at power-on, which a file can't tell apart");
            return 0;
        }
        return 1;
    }
    if (SameState(&state, &sState)) {
        return 1;
    }
    if (videoFrame > PBW_INPUT_MAX_FRAME) {
        Fail("the run went past the last frame a file can name");
        return 0;
    }
    sState = state;
    return WriteState(videoFrame, &state);
}

void port_record_stop_at_seed(size_t taken) {
    fprintf(stderr,
            "port: --record %s: the game asked for random seed %zu, past the %zu the recording declares; stopping "
            "before a seed it didn't record reaches the game\n",
            sPath, taken + 1, taken);
    sFailed = 1;
    port_record_close();
}

int port_record_close(void) {
    int closed;

    if (sFile == NULL) {
        return 1;
    }
    closed = fclose(sFile) == 0;
    sFile = NULL;
    if (!closed) {
        fprintf(stderr, "port: --record %s: can't close it: %s\n", sPath, strerror(errno));
    }
    return closed && !sFailed;
}

void port_record_discard(void) {
    if (sFile != NULL) {
        fclose(sFile);
        sFile = NULL;
        // The file was made by port_record_open, never one that was there before
        remove(sPath);
    }
}

FILE *port_record_stream(void) {
    return sFile;
}

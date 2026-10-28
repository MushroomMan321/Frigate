// Tests of the port's recordings (port/app/record.c, --record): the header a recording starts with, which the input
// file reader (port/tools/oracle/pbwinput.c, linked in) reads back as video frames with the clock and seeds it was
// given; a line for each change of the buttons, lid or touch, none for a state that holds, and a pen that is down for
// one VBlank kept as its own sample; the state at power-on, which a file's frame 1 gives; the file on disk after each
// sample, which parses up to that sample as a killed run would leave it; a write that fails partway, a file that
// exists, a directory that doesn't, and a file removed for a run that never starts. With the argument "exhausted", it
// stops as the program does when the game asks for a seed the recording doesn't declare (port/hal/gfl/random_seed.c,
// linked in), which a test checks with check_exit.py. The files are named after the program (argv[0] plus a suffix),
// so that the plain and sanitized builds can run at the same time.

// POSIX's fileno and dup2, which glibc hides from strict C11
#define _POSIX_C_SOURCE 200809L

#include "app/record.h"
#include "hal/gfl/random_seed.h"
#include "tools/oracle/pbwinput.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static int sFailed;
static int sChecks;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        sChecks++;                                                                                                     \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);                              \
            sFailed++;                                                                                                 \
        }                                                                                                              \
    } while (0)

#define TIME "2012-10-07 12:00:00"

static char sPath[512];
static const uint64_t sSeeds[2] = { 0xea56a175a2fffc8aull, 0x0123456789abcdefull };

static PortRecordStart Start(size_t seedCount, int pinned) {
    PortRecordStart start;

    start.program = "frigate-test";
    start.version = "b2_us";
    start.time = TIME;
    start.pinned = pinned;
    start.input = NULL;
    start.seeds = sSeeds;
    start.seedCount = seedCount;
    return start;
}

static PbwInputState State(uint32_t buttons, int lid, int touching, int x, int y) {
    PbwInputState state = { 0 };

    state.buttons = buttons;
    state.lidClosed = (uint8_t)lid;
    state.touching = (uint8_t)touching;
    state.touchX = (uint8_t)(touching ? x : 0);
    state.touchY = (uint8_t)(touching ? y : 0);
    return state;
}

static int SameState(PbwInputState a, PbwInputState b) {
    return a.buttons == b.buttons && a.lidClosed == b.lidClosed && a.touching == b.touching && a.touchX == b.touchX &&
           a.touchY == b.touchY;
}

static int Sample(uint32_t frame, PbwInputState state) {
    return port_record_vblank(frame, state.buttons, state.lidClosed, state.touching, state.touchX, state.touchY);
}

// The file as it is on disk now, read as --input reads it. Returns 0 when it doesn't parse
static int Load(PbwInput *input) {
    char error[256];

    if (!pbw_input_load(input, sPath, error, sizeof(error))) {
        fprintf(stderr, "%s: %s\n", sPath, error);
        return 0;
    }
    return 1;
}

static int FileHas(const char *line) {
    char text[4096];
    FILE *file = fopen(sPath, "rb");
    size_t length = file != NULL ? fread(text, 1, sizeof(text) - 1, file) : 0;

    if (file != NULL) {
        fclose(file);
    }
    text[length] = '\0';
    return strstr(text, line) != NULL;
}

// A file that can't be made, or one already there, is refused, and the one there keeps its bytes
static void TestRefused(void) {
    char missing[600];
    PortRecordStart start = Start(1, 0);
    FILE *file;

    remove(sPath);
    start.seedCount = 0;
    CHECK(!port_record_open(sPath, &start));
    start = Start(1, 0);
    start.time = "1999-12-31 23:59:59";
    CHECK(!port_record_open(sPath, &start));
    snprintf(missing, sizeof(missing), "%s.missing/recording.pbwinput", sPath);
    start = Start(1, 0);
    CHECK(!port_record_open(missing, &start));
    CHECK(!port_record_active());

    file = fopen(sPath, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("kept\n", file);
        fclose(file);
    }
    CHECK(!port_record_open(sPath, &start));
    CHECK(FileHas("kept\n") && !FileHas("pbwinput"));
    remove(sPath);
}

// The header, then a line for each change, each on disk at once: after every sample the file reads back as the
// states given up to it, which is what a run killed there leaves
static void TestSamples(void) {
    // The states at VBlanks 1 to 14: power-on's at 1, holds that write nothing, the pen down for a single VBlank (as in
    // a pass of two VBlanks, which reads the latest sample), the lid, and the pen down at (0,0), which isn't the pen up
    const PbwInputState states[] = {
        State(0, 0, 0, 0, 0),
        State(0, 0, 0, 0, 0),
        State(PBW_INPUT_A, 0, 0, 0, 0),
        State(PBW_INPUT_A, 0, 0, 0, 0),
        State(0, 0, 1, 10, 20),
        State(0, 0, 0, 0, 0),
        State(PBW_INPUT_B, 0, 1, 255, 191),
        State(PBW_INPUT_B, 0, 1, 255, 191),
        State(PBW_INPUT_B, 0, 1, 254, 191),
        State(0, 1, 0, 0, 0),
        State(0, 1, 0, 0, 0),
        State(0, 0, 0, 0, 0),
        State(0, 0, 1, 0, 0),
        State(0, 0, 0, 0, 0),
    };
    const size_t count = sizeof(states) / sizeof(states[0]);
    PortRecordStart start = Start(2, 1);
    PbwInput input;

    remove(sPath);
    start.input = "routes/aspertia.pbwinput";
    CHECK(port_record_open(sPath, &start));
    CHECK(port_record_active());
    CHECK(port_record_start(0, 0, 0, 0, 0));
    if (Load(&input)) {
        CHECK(input.frames == PBW_INPUT_VIDEO_FRAMES && input.clock == PBW_INPUT_CLOCK_PINNED);
        CHECK(strcmp(input.time, TIME) == 0 && strcmp(input.version, "b2_us") == 0);
        CHECK(input.seedCount == 2 && input.seeds[0] == sSeeds[0] && input.seeds[1] == sSeeds[1]);
        CHECK(input.count == 0);
        pbw_input_free(&input);
    }
    CHECK(FileHas("# Recorded by Frigate: frigate-test, replaying aspertia.pbwinput\n"));
    for (size_t vblank = 1; vblank <= count; vblank++) {
        CHECK(Sample((uint32_t)vblank, states[vblank - 1]));
        if (Load(&input)) {
            for (size_t frame = 1; frame <= vblank; frame++) {
                CHECK(SameState(pbw_input_state(&input, (uint32_t)frame), states[frame - 1]));
            }
            pbw_input_free(&input);
        } else {
            sFailed++;
        }
    }
    CHECK(port_record_close());
    CHECK(!port_record_active());
    // One line a change: A at 3, the pen down at 5 and up at 6, B with the pen at 7 and moved at 9, the lid at 10 and
    // open at 12, the pen at (0,0) at 13 and up at 14
    if (Load(&input)) {
        CHECK(input.count == 9);
        pbw_input_free(&input);
    }
    CHECK(FileHas("\n3 A -\n5 - 10,20\n6 - -\n7 B 255,191\n9 B 254,191\n10 LID -\n12 - -\n13 - 0,0\n14 - -\n"));
    remove(sPath);
}

// What holds from power-on: frame 1's line, written before the first VBlank, and the first VBlank's state must be it
static void TestStartState(void) {
    PortRecordStart start = Start(1, 0);
    PbwInput input;

    remove(sPath);
    CHECK(port_record_open(sPath, &start));
    CHECK(port_record_start(PBW_INPUT_A | PBW_INPUT_START, 0, 1, 3, 4));
    CHECK(FileHas("\n1 START+A 3,4\n") || FileHas("\n1 A+START 3,4\n"));
    CHECK(port_record_vblank(1, PBW_INPUT_A | PBW_INPUT_START, 0, 1, 3, 4));
    CHECK(port_record_vblank(2, 0, 0, 0, 0, 0));
    CHECK(port_record_close());
    if (Load(&input)) {
        CHECK(SameState(pbw_input_state(&input, 1), State(PBW_INPUT_A | PBW_INPUT_START, 0, 1, 3, 4)));
        CHECK(SameState(pbw_input_state(&input, 2), State(0, 0, 0, 0, 0)));
        pbw_input_free(&input);
    }
    remove(sPath);

    // A file can't hold one state at power-on and another at the first VBlank: refused, and the run fails
    CHECK(port_record_open(sPath, &start));
    CHECK(port_record_start(PBW_INPUT_A, 0, 0, 0, 0));
    CHECK(!port_record_vblank(1, 0, 0, 0, 0, 0));
    CHECK(!port_record_vblank(2, PBW_INPUT_B, 0, 0, 0, 0));
    CHECK(!port_record_close());
    remove(sPath);
}

// A write that fails partway: the samples after it are refused, the close says the recording failed, and the file
// holds every line before it
static void TestWriteFailure(void) {
    PortRecordStart start = Start(1, 0);
    PbwInput input;
    int readOnly;

    remove(sPath);
    CHECK(port_record_open(sPath, &start));
    CHECK(port_record_start(0, 0, 0, 0, 0));
    CHECK(port_record_vblank(1, 0, 0, 0, 0, 0));
    CHECK(port_record_vblank(2, PBW_INPUT_A, 0, 0, 0, 0));
    // The file's descriptor becomes one that can't be written
#ifdef _WIN32
    readOnly = _open(sPath, _O_RDONLY);
    CHECK(readOnly >= 0 && _dup2(readOnly, _fileno(port_record_stream())) == 0);
    _close(readOnly);
#else
    readOnly = open(sPath, O_RDONLY);
    CHECK(readOnly >= 0 && dup2(readOnly, fileno(port_record_stream())) >= 0);
    close(readOnly);
#endif
    CHECK(!port_record_vblank(3, PBW_INPUT_B, 0, 0, 0, 0));
    CHECK(!port_record_vblank(4, 0, 0, 0, 0, 0));
    CHECK(!port_record_close());
    if (Load(&input)) {
        CHECK(input.count == 1 && input.changes[0].frame == 2);
        pbw_input_free(&input);
    }
    remove(sPath);
}

// A run that stops before the game starts removes the file it made
static void TestDiscard(void) {
    PortRecordStart start = Start(1, 0);
    FILE *file;

    remove(sPath);
    CHECK(port_record_open(sPath, &start));
    port_record_discard();
    CHECK(!port_record_active());
    file = fopen(sPath, "rb");
    CHECK(file == NULL);
    if (file != NULL) {
        fclose(file);
    }
}

// The game asks for a third seed where the recording declares two: the program's handler, as main.c's, stops
static void Exhausted(size_t taken) {
    port_record_stop_at_seed(taken);
    fflush(stderr);
    _Exit(1);
}

static int RunExhausted(void) {
    PortRecordStart start = Start(2, 0);

    remove(sPath);
    if (!port_record_open(sPath, &start) || !port_record_start(0, 0, 0, 0, 0)) {
        return 2;
    }
    port_random_seed_give(sSeeds, 2);
    port_random_seed_set_exhausted(Exhausted);
    if (port_random_seed_take(1) != sSeeds[0] || port_random_seed_take(2) != sSeeds[1]) {
        return 2;
    }
    port_random_seed_take(3);
    fprintf(stderr, "the third seed reached the game\n");
    return 2;
}

int main(int argc, char **argv) {
    snprintf(sPath, sizeof(sPath), "%s.recording.tmp", argv[0]);
    if (argc > 1 && strcmp(argv[1], "exhausted") == 0) {
        return RunExhausted();
    }
    TestRefused();
    TestSamples();
    TestStartState();
    TestWriteFailure();
    TestDiscard();
    if (sFailed != 0) {
        fprintf(stderr, "%d of %d checks failed\n", sFailed, sChecks);
        return 1;
    }
    printf("%d checks passed\n", sChecks);
    return 0;
}

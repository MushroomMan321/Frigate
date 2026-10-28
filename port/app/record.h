// Recordings (--record FILE): the buttons, lid and touch the game is given, written as an input file ("pbwinput 1",
// port/tools/oracle/README.md, "Recordings") that --input replays to the same frames, so that a bug found by playing
// comes with its reproduction (issue #38). The state is taken at each VBlank after the window's input or a replay's
// has been applied (port/app/main.c's frame hook), where the ARM7's touch service samples it next (port/hal/tp), and
// written on video frames, a line for each change, each line flushed whole, so that a run that is killed leaves a file
// that parses up to its last complete sample. The header holds what the run started from: the clock's effective time
// and whether it is pinned (port/hal/rtc), and the seeds given to the game in place of the ones it builds
// (port/hal/gfl/random_seed.h), which a recording must declare before the game starts: a live run's 64 from the
// host's random source, a replay's own. A replay of the file also needs the same ROM, the same starting saves and a
// compatible build. Plain C types only.
#ifndef PORT_APP_RECORD_H
#define PORT_APP_RECORD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// How many seeds a live recording declares: as many as a file can hold (PBW_INPUT_MAX_SEEDS)
#define PORT_RECORD_LIVE_SEEDS 64

// What a recording's header says about the run's start
typedef struct PortRecordStart {
    const char *program;   // the program's name, for the file's first comment
    const char *version;   // "b2_us" or "w2_us"
    const char *time;      // the clock at power-on, "YYYY-MM-DD HH:MM:SS"
    int pinned;            // nonzero when the clock stays at that time for the whole run
    const char *input;     // the file being replayed, or NULL for the window's input
    const uint64_t *seeds; // the seeds the game is given, in order
    size_t seedCount;      // 1 to PBW_INPUT_MAX_SEEDS
} PortRecordStart;

// Draws a live recording's seeds from the host's random source (port/hal/host). Returns 0 after printing why
int port_record_draw_seeds(uint64_t *seeds, size_t count);

// Creates the file at path, which must not exist (a recording never overwrites a file: not the input it replays, not
// a save), and writes the header. Returns 0 after printing why
int port_record_open(const char *path, const PortRecordStart *start);
// Whether a recording is open
int port_record_active(void);

// The state that holds from power-on, before the game starts: a replay's frame 1, else nothing held, the lid open and
// the pen up. Writes frame 1's line when it isn't that. Returns 0 after printing why when the write fails
int port_record_start(uint32_t buttons, int lidClosed, int touching, int x, int y);
// The state at the VBlank videoFrame, counted from 1 since the game's main(): writes a line when it differs from the
// last one's. At VBlank 1 it must be power-on's, which a file's frame 1 also gives. Returns 0 after printing why when
// it isn't, or when the write fails; the recording then takes no more samples
int port_record_vblank(uint32_t videoFrame, uint32_t buttons, int lidClosed, int touching, int x, int y);

// Says that the game asked for a seed past the ones the file declares (taken of them used), and closes the file, whose
// every line is already written, for port/hal/gfl/random_seed.h's PortRandomSeedExhausted; the caller then stops
void port_record_stop_at_seed(size_t taken);

// Closes the file. Returns 0 after printing why when its last write or the close failed, or when a sample was refused
int port_record_close(void);

// Closes the file and removes it, for a run that stops before the game starts (a bad option found after --record's)
void port_record_discard(void);

// The open file, for the tests that make its writes fail
FILE *port_record_stream(void);

#endif // PORT_APP_RECORD_H

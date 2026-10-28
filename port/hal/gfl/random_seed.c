// The seeds of the game's random numbers on a host (random_seed.h): the seeds an input file gives, taken in order in
// place of the ones the game builds (src/gfl/gf_standard.c's buildSeed, through gf_standard_host.c), and a record of
// every seed the game used, which the run directory reports (port/app/run_dir.c) so that a test can see which seeds a
// run took and how many it built. A recording stops the run at the first request past the given seeds
// (port/app/record.c), before a seed it didn't declare reaches the game.
#include "hal/gfl/random_seed.h"
#include <stdlib.h>

static uint64_t sGiven[PORT_RANDOM_SEED_MAX_GIVEN];
static size_t sGivenCount;
// The seeds the game used, as many as are kept, and how many it built in all
static uint64_t sUsed[PORT_RANDOM_SEED_MAX_KEPT];
static uint8_t sUsedGiven[PORT_RANDOM_SEED_MAX_KEPT];
static size_t sCount;
static PortRandomSeedExhausted sExhausted;

int port_random_seed_give(const uint64_t *seeds, size_t count) {
    size_t i;

    if (count > PORT_RANDOM_SEED_MAX_GIVEN) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        sGiven[i] = seeds[i];
    }
    sGivenCount = count;
    return 1;
}

uint64_t port_random_seed_take(uint64_t built) {
    int given = sCount < sGivenCount;
    uint64_t seed = given ? sGiven[sCount] : built;

    if (!given && sExhausted != NULL) {
        sExhausted(sCount);
        abort();
    }
    if (sCount < PORT_RANDOM_SEED_MAX_KEPT) {
        sUsed[sCount] = seed;
        sUsedGiven[sCount] = (uint8_t)given;
    }
    sCount++;
    return seed;
}

void port_random_seed_set_exhausted(PortRandomSeedExhausted exhausted) {
    sExhausted = exhausted;
}

size_t port_random_seed_count(void) {
    return sCount;
}

int port_random_seed_at(size_t index, uint64_t *seed, int *given) {
    if (index >= sCount || index >= PORT_RANDOM_SEED_MAX_KEPT) {
        return 0;
    }
    *seed = sUsed[index];
    *given = sUsedGiven[index];
    return 1;
}

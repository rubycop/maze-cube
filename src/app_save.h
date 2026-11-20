#pragma once
#include <stdint.h>

// Completion records, per difficulty (0 Easy, 1 Hard). A leaf header: the packer compiles it alone.
#define SAVE_MAGIC   0x4D5A4331u     // "MZC1"
#define SAVE_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
} saveHeader_t;

typedef struct {
    saveHeader_t header;
    uint32_t     latestMs[2];   // latest completion time; 0 = none yet
    uint32_t     bestMs[2];     // fastest completion time; 0 = none yet
} saveRecord_t;

static_assert(sizeof(saveHeader_t) == 8, "the record header is eight bytes on the wire");
static_assert(sizeof(saveRecord_t) == 24, "the record is 24 bytes on the wire - update this when the record changes");

#define APP_SAVE_SIZE sizeof(saveRecord_t)

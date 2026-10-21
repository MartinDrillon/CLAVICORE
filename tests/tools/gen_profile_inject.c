// Outil hôte de diagnostic (incident PROFILE_SYNC_FAILED) : produit la séquence
// PROFILE_BEGIN / CHUNK / COMMIT (ADR-0013) du profil de
// firmware/clavier/src/solenoid_profile_config.h, `pull_duration_us` remplacé,
// sous forme de lignes Tcl OpenOCD `inj <index> <id> <mot0> <mot1>`.
// Utilisé par moteur_swd_profile.ps1 -InjectPullUs pour rejouer le chemin
// COMMIT du Moteur par SWD, sans Clavier. Encodage = celui de shared/protocol.
//
// Usage : gen_profile_inject <pull_duration_us> <transfer_id>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can_protocol.h"
#include "solenoid_profile.h"
#include "solenoid_profile_config.h"

static unsigned entry_index;

static void emit(uint32_t id, const uint8_t d[8]) {
    printf("inj %u 0x%03X 0x%02X%02X%02X%02X 0x%02X%02X%02X%02X\n", entry_index, (unsigned)id,
           d[3], d[2], d[1], d[0], d[7], d[6], d[5], d[4]);
    ++entry_index;
}

int main(int argc, char **argv) {
    solenoid_profile_t profile = k_solenoid_profile;
    uint8_t wire[SOLENOID_PROFILE_WIRE_SIZE];
    uint8_t frame[8];
    size_t length;
    uint8_t transfer_id;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <pull_duration_us> <transfer_id 1..255>\n", argv[0]);
        return 2;
    }
    profile.pull_duration_us = (uint32_t)strtoul(argv[1], NULL, 0);
    transfer_id = (uint8_t)strtoul(argv[2], NULL, 0);
    if (transfer_id == CAN_PROFILE_TRANSFER_ID_BOOT) {
        fprintf(stderr, "transfer_id 0 reserve au boot\n");
        return 2;
    }
    if (solenoid_profile_validate(&profile) != SOLENOID_PROFILE_OK) {
        fprintf(stderr, "profil invalide (validate=%d)\n", (int)solenoid_profile_validate(&profile));
        return 1;
    }
    length = solenoid_profile_encode(&profile, wire, sizeof(wire));
    if (length != SOLENOID_PROFILE_WIRE_SIZE) {
        fprintf(stderr, "encodage invalide (%u octets)\n", (unsigned)length);
        return 1;
    }

    const can_profile_begin_t begin = {
        .proto_version = CAN_PROTO_VERSION,
        .format_version = SOLENOID_PROFILE_FORMAT_VERSION,
        .transfer_id = transfer_id,
        .flags = CAN_PROFILE_FLAG_GLOBAL,
        .payload_len = (uint16_t)length,
        .chunk_size = CAN_PROFILE_CHUNK_SIZE,
        .reserved = 0u,
    };
    if (can_encode_profile_begin(frame, &begin) != CAN_PROTO_OK) {
        return 1;
    }
    emit(CAN_ID_PROFILE_BEGIN, frame);
    for (uint8_t seq = 0u; (size_t)seq * CAN_PROFILE_CHUNK_SIZE < length; ++seq) {
        can_profile_chunk_t chunk = {0};
        const size_t offset = (size_t)seq * CAN_PROFILE_CHUNK_SIZE;
        const size_t remaining = length - offset;
        chunk.transfer_id = transfer_id;
        chunk.seq = seq;
        memcpy(chunk.data, &wire[offset],
               remaining < CAN_PROFILE_CHUNK_SIZE ? remaining : CAN_PROFILE_CHUNK_SIZE);
        if (can_encode_profile_chunk(frame, &chunk) != CAN_PROTO_OK) {
            return 1;
        }
        emit(CAN_ID_PROFILE_CHUNK, frame);
    }
    const can_profile_commit_t commit = {
        .proto_version = CAN_PROTO_VERSION,
        .format_version = SOLENOID_PROFILE_FORMAT_VERSION,
        .transfer_id = transfer_id,
        .reserved = 0u,
        .crc32 = solenoid_profile_crc32(wire, length),
    };
    if (can_encode_profile_commit(frame, &commit) != CAN_PROTO_OK) {
        return 1;
    }
    emit(CAN_ID_PROFILE_COMMIT, frame);
    fprintf(stderr, "profil pull_duration_us=%lu tag=0x%08lX trames=%u\n",
            (unsigned long)profile.pull_duration_us, (unsigned long)commit.crc32, entry_index);
    return 0;
}

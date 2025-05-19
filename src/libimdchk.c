/*
 * libimdchk.c
 *
 * Library implementation for IMD file consistency checking.
 *
 * www.github.com/hharte/libimd
 *
 * Copyright (c) 2025, Howard M. Harte
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

/* Define to enable debug printf statements */
//#define DEBUG_LIBIMDCHK

#include "libimdchk.h" /* Include our public header */

/* --- Debug Macro --- */
#ifdef DEBUG_LIBIMDCHK
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...) do { } while (0)
#endif


/* --- Internal Helper Functions --- */

/* Check for duplicate sector IDs in the Sector Map */
static void check_smap_consistency_internal(ImdTrackInfo* track, ImdChkResults* results) {
    if (!track || !results) return;
    if (track->num_sectors <= 1) {
        return;
    }

    uint8_t seen_ids[32] = { 0 }; /* 256 bits / 8 bits/byte */
#define SEEN_SET(id) (seen_ids[(id)/8] |= (1 << ((id) % 8)))
#define SEEN_GET(id) (seen_ids[(id)/8] & (1 << ((id) % 8)))

    for (int i = 0; i < track->num_sectors; ++i) {
        uint8_t current_id = track->smap[i];
        if (SEEN_GET(current_id)) {
            results->check_failures_mask |= CHECK_BIT_DUPE_SID;
            DEBUG_PRINTF("LIBIMDCHK: Duplicate SID %u at C%u H%u Idx%d\n", current_id, track->cyl, track->head, i);
            /* Unlike report_issue, we don't print multiple times here */
            /* but ensure the failure mask bit is set */
        } else {
            SEEN_SET(current_id);
        }
    }
#undef SEEN_SET
#undef SEEN_GET
}

/* Check validity of sector flags and update statistics */
static void check_sflag_consistency_and_stats_internal(ImdTrackInfo* track, ImdChkResults* results) {
    if (!track || !results) return;

    results->total_sector_count += track->num_sectors;
    int data_error_found = 0;
    int deleted_dam_found = 0;

    if (track->num_sectors == 0) {
        return;
    }

    for (int i = 0; i < track->num_sectors; ++i) {
        uint8_t flag = track->sflag[i];

        /* Check Flag Value Validity */
        if (flag > IMD_SDR_COMPRESSED_DEL_ERR) {
            results->check_failures_mask |= CHECK_BIT_INV_SFLAG_VALUE;
            DEBUG_PRINTF("LIBIMDCHK: Invalid SFlag 0x%02X at C%u H%u Idx%d\n", flag, track->cyl, track->head, i);
        }

        /* Update Statistics */
        if (!IMD_SDR_HAS_DATA(flag)) {
            results->unavailable_sector_count++;
        } else {
            if (IMD_SDR_IS_COMPRESSED(flag)) results->compressed_sector_count++;
            if (IMD_SDR_HAS_DAM(flag)) { results->deleted_sector_count++; deleted_dam_found = 1; }
            if (IMD_SDR_HAS_ERR(flag)) { results->data_error_sector_count++; data_error_found = 1; }
        }
    }

    if (data_error_found) results->check_failures_mask |= CHECK_BIT_SFLAG_DATA_ERR;
    if (deleted_dam_found) results->check_failures_mask |= CHECK_BIT_SFLAG_DEL_DAM;
}

/* Determine interleave from sector map (copied from original imdchk.c) */
static int determine_interleave_internal(const uint8_t* smap, int num_sectors) {
    if (num_sectors < 2) return 1;
    uint8_t first_sector_id = smap[0];
    uint8_t next_logical_id = (first_sector_id == 0) ? 1 : first_sector_id + 1;
    int physical_pos_next = -1;
    for (int i = 1; i < num_sectors; ++i) { if (smap[i] == next_logical_id) { physical_pos_next = i; break; } }
    if (physical_pos_next == -1) {
        uint8_t wrap_around_id = 0;
        if (first_sector_id > 1) {
            uint8_t min_id = 255;
            for (int i = 0; i < num_sectors; ++i) if (smap[i] < min_id) min_id = smap[i];
            wrap_around_id = min_id;
        } else if (first_sector_id == 0) { wrap_around_id = 0; } else { wrap_around_id = 1; }
        for (int i = 1; i < num_sectors; ++i) { if (smap[i] == wrap_around_id) { physical_pos_next = i; break; } }
        if (physical_pos_next == -1) return 0;
    }
    return physical_pos_next;
}


/* --- Public Function Implementation --- */

int imdchk_check_file(const char* filename, const ImdChkOptions* options, ImdChkResults* results) {
    FILE* f_imd = NULL;
    ImdHeaderInfo header_info; /* Use struct from libimd.h */
    ImdTrackInfo track;        /* Use struct from libimd.h */
    int result;
    uint8_t last_cyl = 0;
    uint8_t last_head = 1;     /* Initialize to invalid state */
    int first_track = 1;

    if (!filename || !options || !results) {
        return -1; /* Invalid arguments */
    }

    /* Initialize results structure */
    memset(results, 0, sizeof(ImdChkResults));
    results->max_cyl_side0 = -1;
    results->max_cyl_side1 = -1;
    results->max_head_seen = -1;
    results->detected_interleave = -1;

    f_imd = fopen(filename, "rb");
    if (!f_imd) {
        /* Cannot report failure through results struct, return error */
        return -1;
    }

    /* Read Header using libimd */
    result = imd_read_file_header(f_imd, &header_info, NULL, 0);
    if (result != 0) {
        results->check_failures_mask |= CHECK_BIT_HEADER;
        if (options->error_mask & CHECK_BIT_HEADER) {
            fclose(f_imd);
            return -1; /* Treat as critical file error if required by mask */
        }
    }

    /* Skip Comment Block using libimd */
    result = imd_skip_comment_block(f_imd);
    if (result != 0) {
        results->check_failures_mask |= CHECK_BIT_COMMENT_TERM;
        if (options->error_mask & CHECK_BIT_COMMENT_TERM) {
            fclose(f_imd);
            return -1; /* Treat as critical file error if required by mask */
        }
    }

    /* Loop reading tracks using libimd */
    while (1) {
        long track_start_pos = ftell(f_imd);
        if (track_start_pos < 0) {
            results->check_failures_mask |= CHECK_BIT_FTELL;
            if (options->error_mask & CHECK_BIT_FTELL) break; /* Stop loop if fatal */
            else continue; /* Otherwise try next track? Better to break. */
            break;
        }

        memset(&track, 0, sizeof(ImdTrackInfo)); /* Clear track struct */
        result = imd_read_track_header_and_flags(f_imd, &track);

        if (result == 0) break; /* Clean EOF */
        if (result < 0) {
            results->check_failures_mask |= CHECK_BIT_TRACK_READ;
            if (options->error_mask & CHECK_BIT_TRACK_READ) break; /* Stop processing */
            else continue; /* Skip this track if non-fatal */
        }

        /* Track read successful */
        results->track_read_count++;

        /* Apply Command Line Constraints */
        int constraint_failed = 0;
        if (options->max_allowed_cyl != -1 && track.cyl > options->max_allowed_cyl) {
            results->check_failures_mask |= CHECK_BIT_CON_CYL; constraint_failed = 1;
        }
        if (options->required_head != -1 && track.head != options->required_head) {
            results->check_failures_mask |= CHECK_BIT_CON_HEAD; constraint_failed = 1;
        }
        if (options->max_allowed_sectors != -1 && track.num_sectors > options->max_allowed_sectors) {
            results->check_failures_mask |= CHECK_BIT_CON_SECTORS; constraint_failed = 1;
        }
        /* If a constraint failed and is considered an error, skip further checks for this track */
        if (constraint_failed && (options->error_mask & (CHECK_BIT_CON_CYL | CHECK_BIT_CON_HEAD | CHECK_BIT_CON_SECTORS))) {
            continue;
        }

        /* Update Summary Information */
        if (track.head == 0) { if ((int)track.cyl > results->max_cyl_side0) results->max_cyl_side0 = track.cyl; }
        else if (track.head == 1) { if ((int)track.cyl > results->max_cyl_side1) results->max_cyl_side1 = track.cyl; }
        if ((int)track.head > results->max_head_seen) results->max_head_seen = track.head;
        if (results->detected_interleave == -1 && track.num_sectors > 0) {
            results->detected_interleave = determine_interleave_internal(track.smap, track.num_sectors);
        }

        /* Perform Consistency Checks & Update Stats */
        if (!first_track) {
           if (track.cyl < last_cyl) {
                results->check_failures_mask |= CHECK_BIT_SEQ_CYL_DEC;
            }
            if (track.cyl == last_cyl && track.head <= last_head && !(track.head == 0 && last_head > 0)) {
                results->check_failures_mask |= CHECK_BIT_SEQ_HEAD_ORDER;
            }
            /* If sequence check failed and is error, skip rest */
            if (options->error_mask & (CHECK_BIT_SEQ_CYL_DEC | CHECK_BIT_SEQ_HEAD_ORDER) & results->check_failures_mask) {
                continue;
            }
        }
        last_cyl = track.cyl; last_head = track.head; first_track = 0;

        check_smap_consistency_internal(&track, results);
        if (options->error_mask & CHECK_BIT_DUPE_SID & results->check_failures_mask) continue;

        check_sflag_consistency_and_stats_internal(&track, results);
        /* If flag checks failed and are errors, skip rest? No, let all checks run per track. */

    } /* End while loop */

    fclose(f_imd);

    /* Final check for Diff Max Cyl */
    if (results->max_head_seen > 0) {
        if (results->max_cyl_side0 != -1 && results->max_cyl_side1 != -1 && results->max_cyl_side0 != results->max_cyl_side1) {
            results->check_failures_mask |= CHECK_BIT_DIFF_MAX_CYL;
        }
    }

    return 0; /* Indicates file was processed */
}

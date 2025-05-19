/*
 * libimdchk.h
 *
 * Library definitions for IMD file consistency checking.
 *
 * www.github.com/hharte/libimd
 *
 * Copyright (c) 2025, Howard M. Harte
 */

#ifndef LIBIMDCHK_H
#define LIBIMDCHK_H

#include <stdint.h>
#include <stdio.h> /* For FILE type */
#include "libimd.h" /* Dependency */

#ifdef __cplusplus
extern "C" {
#endif

/* --- Bitmask Definitions for Checks --- */
/* Errors by default */
#define CHECK_BIT_HEADER          0x00000001U /* Error: Invalid Header */
#define CHECK_BIT_COMMENT_TERM    0x00000002U /* Error: Missing Comment Terminator */
#define CHECK_BIT_TRACK_READ      0x00000004U /* Error: Track Read Failure */
#define CHECK_BIT_FTELL           0x00000008U /* Error: ftell Failure */
#define CHECK_BIT_CON_CYL         0x00000010U /* Error: Cylinder Constraint Violation */
#define CHECK_BIT_CON_HEAD        0x00000020U /* Error: Head Constraint Violation */
#define CHECK_BIT_CON_SECTORS     0x00000040U /* Error: Sector Count Constraint Violation */
#define CHECK_BIT_DUPE_SID        0x00000200U /* Error: Duplicate Sector ID */
#define CHECK_BIT_INV_SFLAG_VALUE 0x00000400U /* Error: Invalid Sector Flag Value (>0x08) */
/* Warnings by default */
#define CHECK_BIT_SEQ_CYL_DEC     0x00000080U /* Warning: Cylinder Sequence Decrease */
#define CHECK_BIT_SEQ_HEAD_ORDER  0x00000100U /* Warning: Head Sequence Out of Order */
#define CHECK_BIT_SFLAG_DATA_ERR  0x00000800U /* Warning: Data Error Flag Set (0x05-0x08) */
#define CHECK_BIT_SFLAG_DEL_DAM   0x00001000U /* Warning: Deleted DAM Flag Set (0x03,0x04,0x07,0x08) */
#define CHECK_BIT_DIFF_MAX_CYL    0x00002000U /* Warning: Max Cylinder Differs Between Sides */

/* Default mask: Treat original errors as errors, original warnings as warnings */
#define DEFAULT_ERROR_MASK (CHECK_BIT_HEADER | CHECK_BIT_COMMENT_TERM | CHECK_BIT_TRACK_READ | \
                            CHECK_BIT_FTELL | CHECK_BIT_CON_CYL | CHECK_BIT_CON_HEAD | \
                            CHECK_BIT_CON_SECTORS | CHECK_BIT_DUPE_SID | CHECK_BIT_INV_SFLAG_VALUE)

/* --- Data Structures --- */

/* Options for the checking process */
typedef struct {
    uint32_t error_mask;         /* Bitmask indicating which check failures are errors */
    long max_allowed_cyl;    /* Constraint: Max cylinder (-1 if disabled) */
    long required_head;      /* Constraint: Required head (0 or 1, -1 if disabled) */
    long max_allowed_sectors;/* Constraint: Max sectors per track (-1 if disabled) */
} ImdChkOptions;

/* Results of the checking process */
typedef struct {
    uint32_t check_failures_mask;   /* Bitmask of performed checks that failed */
    /* Statistics */
    long long total_sector_count;
    long long unavailable_sector_count;
    long long deleted_sector_count;
    long long compressed_sector_count;
    long long data_error_sector_count;
    int track_read_count;
    int max_cyl_side0;
    int max_cyl_side1;
    int max_head_seen;
    int detected_interleave; /* -1 = N/A, 0 = Unknown, >0 = Factor */
    /* Optionally add more detailed info if needed, e.g., list of failed tracks */
} ImdChkResults;

/* --- Public Function Prototypes --- */

/**
 * @brief Checks the consistency of an IMD file based on provided options.
 * @param filename The path to the IMD file to check.
 * @param options Pointer to the ImdChkOptions structure containing check parameters.
 * @param results Pointer to the ImdChkResults structure to store the outcome.
 * @return 0 if the file was opened and processed (even if checks failed),
 * -1 on file open error or critical read error preventing processing.
 */
int imdchk_check_file(const char* filename, const ImdChkOptions* options, ImdChkResults* results);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBIMDCHK_H */

/*
 * In-Memory ImageDisk File Library Implementation.
 *
 * www.github.com/hharte/libimd
 *
 * Copyright (c) 2025, Howard M. Harte
 */

 /* Define _DEFAULT_SOURCE to enable POSIX features like strdup */
#define _DEFAULT_SOURCE

#include "libimdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Check if strdup is available (needed for POSIX compliance) */
#ifdef _WIN32
#define strdup _strdup
#else /* Assume POSIX */
#include <sys/types.h>
#include <unistd.h>
#endif

/* Define to enable debug printf statements */
//#define DEBUG_LIBIMDF   /* Uncomment for debug */

/* --- Debug Macro --- */
#ifdef DEBUG_LIBIMDF
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...) do { } while (0)
#endif

/* Initial capacity for the tracks array */
#define IMDF_INITIAL_TRACK_CAPACITY 80 /* Default to 80 tracks (e.g. 40 cyl, 2 heads) */

/* Default WriteOpts for internal use in libimdf when writing tracks */
static const ImdWriteOpts default_libimdf_write_opts = {
    IMD_COMPRESSION_AS_READ, /* Default to AS_READ for general rewrites */
    0,                       /* force_non_bad = false */
    0,                       /* force_non_deleted = false */
    {0, 1, 2, 3, 4, 5},      /* tmode (identity map) */
    LIBIMD_IL_AS_READ        /* interleave_factor = as read/provided */
};


/* --- Internal Data Structure --- */
struct ImdImageFile {
    FILE* file_ptr;             /* Handle to the open IMD file */
    char* file_path;            /* Stored path for potential reopening */
    int write_protected;        /* Write protection status */
    int read_only_open;         /* Was the file opened with read_only flag? */

    ImdHeaderInfo header_info;  /* Parsed header info */
    char* comment;              /* Comment block */
    size_t comment_len;         /* Length of comment */

    ImdTrackInfo* tracks;       /* Dynamic array of loaded tracks */
    size_t num_tracks;          /* Number of tracks currently loaded */
    size_t track_capacity;      /* Allocated capacity of the tracks array */

    /* Geometry limits */
    uint8_t max_cyl;            /* Set to 0xFF if unused */
    uint8_t max_head;           /* Set to 0xFF if unused */
    uint8_t max_spt;            /* Set to 0xFF if unused */
};

/* --- Internal Helper Functions --- */

/* Finds track index by C/H. Returns -1 if not found. */
static int find_track_index_internal(const ImdImageFile* imdf, uint8_t cyl, uint8_t head) {
    if (!imdf) return -1;
    for (size_t i = 0; i < imdf->num_tracks; ++i) {
        if (imdf->tracks[i].cyl == cyl && imdf->tracks[i].head == head) {
            return (int)i;
        }
    }
    return -1;
}

/* Finds the physical index of a sector given its logical ID. Returns -1 if not found. */
int find_sector_index_internal(const ImdTrackInfo* track, uint8_t logical_sector_id) {
    if (!track) return -1;
    for (uint8_t i = 0; i < track->num_sectors; ++i) {
        if (track->smap[i] == logical_sector_id) {
            return i;
        }
    }
    return -1;
}

/* Converts libimd error code to libimdf error code */
static int map_libimd_error(int imd_err) {
    switch (imd_err) {
    case 0: return IMDF_ERR_OK; /* libimd uses 0 for some successes, 1 for others */
    case 1: return IMDF_ERR_OK; /* Treat positive as OK from libimd load/read_header */
    case IMD_ERR_READ_ERROR:
    case IMD_ERR_WRITE_ERROR:
    case IMD_ERR_SEEK_ERROR:
        return IMDF_ERR_IO;
    case IMD_ERR_ALLOC:
        return IMDF_ERR_ALLOC;
    case IMD_ERR_INVALID_ARG:
        return IMDF_ERR_INVALID_ARG;
    case IMD_ERR_BUFFER_TOO_SMALL:
        return IMDF_ERR_BUFFER_SIZE;
    case IMD_ERR_SECTOR_NOT_FOUND:
    case IMD_ERR_TRACK_NOT_FOUND:
        return IMDF_ERR_NOT_FOUND;
    case IMD_ERR_UNAVAILABLE:
        return IMDF_ERR_UNAVAILABLE;
    case IMD_ERR_SIZE_MISMATCH: /* Map to sector size error */
        return IMDF_ERR_SECTOR_SIZE;
    default:
        DEBUG_PRINTF("map_libimd_error: Unmapped libimd error %d\n", imd_err);
        return IMDF_ERR_LIBIMD_ERR; /* Generic internal error */
    }
}

/*
 * Rewrites the entire IMD file from the in-memory structures.
 * This is necessary after modifications to ensure file consistency.
 * Applies specific write options for a potentially modified track.
 * If modified_track_index is >= num_tracks, it implies no specific opts, use default for all.
 */
static int rewrite_image_file(ImdImageFile* imdf, size_t modified_track_index, const ImdWriteOpts* modified_track_opts) {
    if (!imdf || !imdf->file_ptr) { /* No write_protected check, caller should do it */
        return IMDF_ERR_INVALID_ARG;
    }

    DEBUG_PRINTF("Rewriting image file '%s' (modified index: %zu)\n", imdf->file_path, modified_track_index);

    /* Seek to beginning to overwrite */
    if (fseek(imdf->file_ptr, 0, SEEK_SET) != 0) {
        perror("libimdf: fseek failed before rewrite");
        return IMDF_ERR_IO;
    }

    /* Write Header */
    const char* version_to_write = NULL;
    /* Use a known valid default if the loaded version is empty or the specific "Unknown" placeholder */
    if (imdf->header_info.version[0] == '\0' ||
        strcmp(imdf->header_info.version, "Unknown") == 0)
    {
        version_to_write = "1.19"; /* Default valid version */
        DEBUG_PRINTF("Rewrite: Using default version '%s' because loaded version was invalid/empty ('%s').\n",
            version_to_write, imdf->header_info.version);
    }
    else {
        /* Otherwise, use the version string loaded from the original header */
        version_to_write = imdf->header_info.version;
        DEBUG_PRINTF("Rewrite: Using loaded version '%s'.\n", version_to_write);
    }
    int res = imd_write_file_header(imdf->file_ptr, version_to_write);
    if (res != 0) {
        DEBUG_PRINTF("Rewrite failed: imd_write_file_header returned %d\n", res);
        return map_libimd_error(res);
    }

    /* Write Comment */
    res = imd_write_comment_block(imdf->file_ptr, imdf->comment, imdf->comment_len);
    if (res != 0) {
        DEBUG_PRINTF("Rewrite failed: imd_write_comment_block returned %d\n", res);
        return map_libimd_error(res);
    }

    /* Write Tracks */
    for (size_t i = 0; i < imdf->num_tracks; ++i) {
        const ImdWriteOpts* opts_to_use = &default_libimdf_write_opts;
        if (i == modified_track_index && modified_track_opts != NULL) {
            opts_to_use = modified_track_opts;
            DEBUG_PRINTF("Using modified opts for track %zu (C%u H%u)\n", i, imdf->tracks[i].cyl, imdf->tracks[i].head);
        }
        else {
            DEBUG_PRINTF("Using default opts for track %zu (C%u H%u)\n", i, imdf->tracks[i].cyl, imdf->tracks[i].head);
        }

        if (!imdf->tracks[i].loaded) {
            DEBUG_PRINTF("Rewrite Error: Track %zu (C%u H%u) not marked as loaded!\n", i, imdf->tracks[i].cyl, imdf->tracks[i].head);
            return IMDF_ERR_LIBIMD_ERR; /* Internal state error */
        }

        res = imd_write_track_imd(imdf->file_ptr, &imdf->tracks[i], opts_to_use);
        if (res != 0) {
            DEBUG_PRINTF("Rewrite failed: imd_write_track_imd for track %zu (C%u H%u) returned %d\n",
                i, imdf->tracks[i].cyl, imdf->tracks[i].head, res);
            return map_libimd_error(res);
        }
    }

    /* Flush all buffered writes to ensure ftell gets the correct current position */
    if (fflush(imdf->file_ptr) != 0) {
        perror("libimdf: fflush failed before getting size for truncate");
        DEBUG_PRINTF("Rewrite Error: fflush failed before truncate, error %d\n", errno);
        return IMDF_ERR_IO; /* Treat this as a fatal error */
    }

    long current_pos = ftell(imdf->file_ptr);
    if (current_pos < 0) {
        perror("libimdf: ftell failed before truncate attempt");
        DEBUG_PRINTF("Rewrite Warning: ftell failed, cannot get size to truncate file, error %d\n", errno);
        /* Continue, but truncation won't happen; file might be left with garbage at the end */
    }
    else {
        int fd = -1;
        int trunc_res = -1;
        DEBUG_PRINTF("Rewrite: Attempting to truncate file at offset %ld.\n", current_pos);

#ifdef _WIN32
        /* Need <io.h> for _fileno, _chsize */
#include <io.h>
        fd = _fileno(imdf->file_ptr);
        if (fd != -1) {
            trunc_res = _chsize(fd, current_pos);
            if (trunc_res != 0) {
                perror("libimdf: _chsize failed after rewrite");
                DEBUG_PRINTF("Rewrite Warning: _chsize failed (errno %d), file may contain old data at end.\n", errno);
            }
            else {
                DEBUG_PRINTF("Rewrite: Successfully truncated file (Windows).\n");
            }
        }
        else {
            DEBUG_PRINTF("Rewrite Warning: _fileno failed, cannot truncate file (Windows).\n");
        }
#else /* Assume POSIX */
        fd = fileno(imdf->file_ptr);
        if (fd != -1) {
            trunc_res = ftruncate(fd, (off_t)current_pos);
            if (trunc_res != 0) {
                perror("libimdf: ftruncate failed after rewrite");
                DEBUG_PRINTF("Rewrite Warning: ftruncate failed (errno %d), file may contain old data at end.\n", errno);
            }
            else {
                DEBUG_PRINTF("Rewrite: Successfully truncated file (POSIX).\n");
            }
        }
        else {
            DEBUG_PRINTF("Rewrite Warning: fileno failed, cannot truncate file (POSIX).\n");
        }
#endif
    }

    /* Final flush might not be strictly necessary if the file was just truncated
       and no further writes occurred, but ensures state.
       Some systems might require it if file descriptors were manipulated.
    */
    if (fflush(imdf->file_ptr) != 0) {
        perror("libimdf: fflush failed after rewrite/truncate attempt");
        DEBUG_PRINTF("Rewrite Error: final fflush failed, error %d\n", errno);
        return IMDF_ERR_IO;
    }

    DEBUG_PRINTF("Image file rewrite successful.\n");
    return IMDF_ERR_OK;
}

/* Finds the correct insertion index for a new track to maintain C/H order */
static size_t find_insertion_index(const ImdImageFile* imdf, uint8_t cyl, uint8_t head) {
    size_t low = 0, high = imdf->num_tracks;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (imdf->tracks[mid].cyl < cyl || (imdf->tracks[mid].cyl == cyl && imdf->tracks[mid].head < head)) {
            low = mid + 1;
        }
        else {
            high = mid;
        }
    }
    return low; /* Index where the new track should be inserted */
}

/* Helper to get sector size code from bytes */
int get_sector_size_code(uint32_t sector_size, uint8_t* code_out) {
    size_t lookup_count;
    const uint32_t* lookup = imd_get_sector_size_lookup(&lookup_count);
    if (!lookup) return -1; /* Should not happen */
    for (size_t i = 0; i < lookup_count; ++i) {
        if (lookup[i] == sector_size) {
            *code_out = (uint8_t)i;
            return 0; /* Success */
        }
    }
    return -1; /* Not found */
}


/* --- Public Function Implementations --- */

/* --- Image Handling --- */

int imdf_open(const char* path, int read_only, ImdImageFile** imdf_out) {
    FILE* f = NULL;
    ImdImageFile* imdf = NULL;
    int libimd_err;
    int result;

    if (!path || !imdf_out) {
        return IMDF_ERR_INVALID_ARG;
    }
    *imdf_out = NULL;

    const char* mode = read_only ? "rb" : "r+b";
    f = fopen(path, mode);
    if (!f) {
        DEBUG_PRINTF("imdf_open: fopen('%s', '%s') failed: %s\n", path, mode, strerror(errno));
        return IMDF_ERR_CANNOT_OPEN;
    }

    imdf = (ImdImageFile*)calloc(1, sizeof(ImdImageFile));
    if (!imdf) {
        fclose(f);
        return IMDF_ERR_ALLOC;
    }

    imdf->file_ptr = f;
    imdf->read_only_open = (read_only != 0);
    imdf->write_protected = (read_only != 0); /* Initially protected if opened read-only */
    imdf->max_cyl = 0xFF; /* Unused by default */
    imdf->max_head = 0xFF;/* Unused by default */
    imdf->max_spt = 0xFF; /* Unused by default */

    /* Store file path */
    imdf->file_path = strdup(path);
    if (!imdf->file_path) {
        result = IMDF_ERR_ALLOC;
        goto cleanup_error;
    }

    /* Read Header */
    libimd_err = imd_read_file_header(imdf->file_ptr, &imdf->header_info, NULL, 0);
    if (libimd_err != 0) {
        result = map_libimd_error(libimd_err);
        DEBUG_PRINTF("imdf_open: imd_read_file_header failed (%d)\n", libimd_err);
        goto cleanup_error;
    }

    /* Read Comment */
    imdf->comment = imd_read_comment_block(imdf->file_ptr, &imdf->comment_len);

    if (imdf->comment == NULL) {
        if (ferror(imdf->file_ptr)) {
            DEBUG_PRINTF("imdf_open: I/O error after imd_read_comment_block returned NULL.\n");
            result = IMDF_ERR_IO;
        }
        else {
            DEBUG_PRINTF("imdf_open: imd_read_comment_block returned NULL (alloc error or EOF before comment terminator?).\n");
            result = IMDF_ERR_LIBIMD_ERR;
        }
        goto cleanup_error;
    }

    /* Read Tracks */
    imdf->num_tracks = 0;
    imdf->track_capacity = IMDF_INITIAL_TRACK_CAPACITY;

    imdf->tracks = (ImdTrackInfo*)malloc(imdf->track_capacity * sizeof(ImdTrackInfo));
    if (!imdf->tracks) {
        result = IMDF_ERR_ALLOC;
        goto cleanup_error;
    }
    memset(imdf->tracks, 0, imdf->track_capacity * sizeof(ImdTrackInfo));


    DEBUG_PRINTF("imdf_open: Reading tracks...\n");
    while (1) {
        if (imdf->num_tracks >= imdf->track_capacity) {
            size_t new_capacity = imdf->track_capacity * 2;
            if (new_capacity <= imdf->track_capacity) {
                result = IMDF_ERR_ALLOC;
                goto cleanup_error;
            }
            ImdTrackInfo* new_tracks = (ImdTrackInfo*)realloc(imdf->tracks, new_capacity * sizeof(ImdTrackInfo));
            if (!new_tracks) {
                result = IMDF_ERR_ALLOC;
                goto cleanup_error;
            }
            imdf->tracks = new_tracks;
            imdf->track_capacity = new_capacity;
            DEBUG_PRINTF("imdf_open: Reallocated track array to %zu\n", new_capacity);
        }

        ImdTrackInfo* current_track = &imdf->tracks[imdf->num_tracks];
        memset(current_track, 0, sizeof(ImdTrackInfo));
        libimd_err = imd_load_track(imdf->file_ptr, current_track, LIBIMD_FILL_BYTE_DEFAULT);

        if (libimd_err == 1) { /* Success */
            DEBUG_PRINTF("  Loaded track %zu: C%u H%u Ns%u Sz%u Code%u Mode%u Hflag0x%02X\n",
                imdf->num_tracks, current_track->cyl, current_track->head,
                current_track->num_sectors, current_track->sector_size,
                current_track->sector_size_code, current_track->mode, current_track->hflag);

            imdf->num_tracks++;
        }
        else if (libimd_err == 0) { /* Clean EOF */
            DEBUG_PRINTF("imdf_open: EOF reached after %zu tracks.\n", imdf->num_tracks);
            break;
        }
        else { /* Error */
            DEBUG_PRINTF("imdf_open: imd_load_track failed (%d) for logical track %zu\n", libimd_err, imdf->num_tracks);
            result = map_libimd_error(libimd_err);
            goto cleanup_error;
        }
    }

    *imdf_out = imdf;
    return IMDF_ERR_OK;

cleanup_error:
    DEBUG_PRINTF("imdf_open: Cleaning up after error %d\n", result);
    if (imdf) {
        if (imdf->tracks) {
            for (size_t i = 0; i < imdf->num_tracks; ++i) {
                imd_free_track_data(&imdf->tracks[i]);
            }
            free(imdf->tracks);
        }
        if (imdf->comment) {
            free(imdf->comment);
        }
        if (imdf->file_path) {
            free(imdf->file_path);
        }
        free(imdf);
    }
    if (f) {
        fclose(f);
    }
    *imdf_out = NULL;
    return result;
}

void imdf_close(ImdImageFile* imdf) {
    if (!imdf) {
        return;
    }
    DEBUG_PRINTF("Closing image file: %s\n", imdf->file_path ? imdf->file_path : "(no path)");
    if (imdf->tracks) {
        for (size_t i = 0; i < imdf->num_tracks; ++i) {
            imd_free_track_data(&imdf->tracks[i]);
        }
        free(imdf->tracks);
    }
    if (imdf->comment) {
        free(imdf->comment);
    }
    if (imdf->file_ptr) {
        fclose(imdf->file_ptr);
    }
    if (imdf->file_path) {
        free(imdf->file_path);
    }
    free(imdf);
}

/* --- Geometry --- */

int imdf_set_geometry(ImdImageFile* imdf, uint8_t max_cyl, uint8_t max_head, uint8_t max_spt) {
    if (!imdf) return IMDF_ERR_INVALID_ARG;
    imdf->max_cyl = max_cyl;
    imdf->max_head = max_head;
    imdf->max_spt = max_spt;
    DEBUG_PRINTF("Set geometry: Cmax=%u Hmax=%u SptMax=%u\n", max_cyl, max_head, max_spt);
    return IMDF_ERR_OK;
}

int imdf_get_geometry(ImdImageFile* imdf, uint8_t* max_cyl_out, uint8_t* max_head_out, uint8_t* max_spt_out) {
    if (!imdf) return IMDF_ERR_INVALID_ARG;
    if (max_cyl_out) *max_cyl_out = imdf->max_cyl;
    if (max_head_out) *max_head_out = imdf->max_head;
    if (max_spt_out) *max_spt_out = imdf->max_spt;
    return IMDF_ERR_OK;
}

/* --- Write Protection --- */

int imdf_set_write_protect(ImdImageFile* imdf, int protect) {
    if (!imdf) return IMDF_ERR_INVALID_ARG;
    if (!protect && imdf->read_only_open) {
        return IMDF_ERR_WRITE_PROTECTED; /* Cannot unprotect a read-only opened file */
    }
    imdf->write_protected = (protect != 0);
    DEBUG_PRINTF("Set write protect: %d\n", imdf->write_protected);
    return IMDF_ERR_OK;
}

int imdf_get_write_protect(ImdImageFile* imdf, int* protect_out) {
    if (!imdf || !protect_out) return IMDF_ERR_INVALID_ARG;
    *protect_out = imdf->write_protected;
    return IMDF_ERR_OK;
}

/* --- Metadata Access --- */

const ImdHeaderInfo* imdf_get_header_info(const ImdImageFile* imdf) {
    return imdf ? &imdf->header_info : NULL;
}

const char* imdf_get_comment(const ImdImageFile* imdf, size_t* comment_len_out) {
    if (!imdf) return NULL;
    if (comment_len_out) *comment_len_out = imdf->comment_len;
    return imdf->comment;
}

int imdf_get_num_tracks(const ImdImageFile* imdf, size_t* num_tracks_out) {
    if (!imdf || !num_tracks_out) return IMDF_ERR_INVALID_ARG;
    *num_tracks_out = imdf->num_tracks;
    return IMDF_ERR_OK;
}

const ImdTrackInfo* imdf_get_track_info(const ImdImageFile* imdf, size_t track_index) {
    if (!imdf || track_index >= imdf->num_tracks) {
        return NULL;
    }
    return &imdf->tracks[track_index];
}

int imdf_find_track_by_ch(const ImdImageFile* imdf, uint8_t cyl, uint8_t head, size_t* track_index_out) {
    if (!imdf || !track_index_out) return IMDF_ERR_INVALID_ARG;
    int index = find_track_index_internal(imdf, cyl, head);
    if (index < 0) {
        return IMDF_ERR_NOT_FOUND;
    }
    *track_index_out = (size_t)index;
    return IMDF_ERR_OK;
}


/* --- Sector Access --- */

int imdf_read_sector(ImdImageFile* imdf, uint8_t cyl, uint8_t head, uint8_t logical_sector_id, uint8_t* buffer, size_t buffer_size) {
    int track_idx;
    int sector_idx;
    ImdTrackInfo* track;

    if (!imdf || !buffer) return IMDF_ERR_INVALID_ARG;

    if ((imdf->max_cyl != 0xFF && cyl > imdf->max_cyl) ||
        (imdf->max_head != 0xFF && head > imdf->max_head) ||
        (imdf->max_spt != 0xFF && logical_sector_id > imdf->max_spt && logical_sector_id != 0)) { /* Allow logical_sector_id 0 if max_spt is 0xFF (unused) or if it's within range */
        DEBUG_PRINTF("Read Sector Error: C%u/H%u/LogSectID %u exceeds geometry Cmax%u/Hmax%u/SptMax%u\n",
            cyl, head, logical_sector_id, imdf->max_cyl, imdf->max_head, imdf->max_spt);
        return IMDF_ERR_GEOMETRY;
    }

    track_idx = find_track_index_internal(imdf, cyl, head);
    if (track_idx < 0) return IMDF_ERR_NOT_FOUND;
    track = &imdf->tracks[track_idx];

    sector_idx = find_sector_index_internal(track, logical_sector_id);
    if (sector_idx < 0) return IMDF_ERR_NOT_FOUND;

    if (track->sflag[sector_idx] == IMD_SDR_UNAVAILABLE) {
        return IMDF_ERR_UNAVAILABLE;
    }

    if (buffer_size < track->sector_size) {
        return IMDF_ERR_BUFFER_SIZE;
    }
    if (!track->data || track->data_size < ((size_t)sector_idx * track->sector_size) + track->sector_size) {
        DEBUG_PRINTF("Read Error: Track data inconsistent for C%u H%u LogSectID %u (Phys %d). DataSize %zu, Expected at least %zu\n",
            cyl, head, logical_sector_id, sector_idx, track->data_size, ((size_t)sector_idx * track->sector_size) + track->sector_size);
        return IMDF_ERR_LIBIMD_ERR;
    }

    memcpy(buffer, track->data + ((size_t)sector_idx * track->sector_size), track->sector_size);

    return IMDF_ERR_OK;
}

/* In libimdf.c */

int imdf_write_sector(ImdImageFile* imdf, uint8_t cyl, uint8_t head, uint8_t logical_sector_id, const uint8_t* buffer, size_t buffer_size) {
    int track_idx_int; /* Renamed to avoid conflict with track_idx size_t */
    size_t track_idx;  /* To store the result from imdf_find_track_by_ch or find_track_index_internal */
    int sector_idx; /* Physical index of the sector being written */
    ImdTrackInfo* track;
    int rewrite_res;
    uint8_t original_sflag_of_edited_sector;
    int was_edited_sector_compressed;
    int track_rewritten_as_uncompressed = 0; /* Flag to indicate if the entire track was forced uncompressed */

    if (!imdf || !buffer) return IMDF_ERR_INVALID_ARG;
    if (imdf->write_protected) return IMDF_ERR_WRITE_PROTECTED;

    /* Validate CHN against geometry if set */
    if ((imdf->max_cyl != 0xFF && cyl > imdf->max_cyl) ||
        (imdf->max_head != 0xFF && head > imdf->max_head)) {
        DEBUG_PRINTF("LibIMDF Write Sector Error: C%u/H%u exceeds geometry Cmax%u/Hmax%u\n", cyl, head, imdf->max_cyl, imdf->max_head);
        return IMDF_ERR_GEOMETRY;
    }

    track_idx_int = find_track_index_internal(imdf, cyl, head);
    if (track_idx_int < 0) return IMDF_ERR_NOT_FOUND;
    track_idx = (size_t)track_idx_int;
    track = &imdf->tracks[track_idx];

    sector_idx = find_sector_index_internal(track, logical_sector_id);
    if (sector_idx < 0) return IMDF_ERR_NOT_FOUND;

    /* Validate sector ID against max_spt if set */
    if (imdf->max_spt != 0xFF && logical_sector_id > imdf->max_spt && logical_sector_id != 0) { /* Allow logical_sector_id 0 if max_spt is 0xFF */
        DEBUG_PRINTF("LibIMDF Write Sector Error: Logical Sector ID %u exceeds SptMax %u\n", logical_sector_id, imdf->max_spt);
        return IMDF_ERR_GEOMETRY;
    }

    if (buffer_size != track->sector_size) {
        DEBUG_PRINTF("LibIMDF Write Sector Error: Buffer size %zu does not match track sector size %u\n", buffer_size, track->sector_size);
        return IMDF_ERR_SECTOR_SIZE;
    }
    if (!track->data || track->data_size < ((size_t)sector_idx * track->sector_size) + track->sector_size) {
        DEBUG_PRINTF("LibIMDF Write Sector Error: Track data inconsistent for C%u H%u LogSectID %u (Phys %d)\n", cyl, head, logical_sector_id, sector_idx);
        return IMDF_ERR_LIBIMD_ERR; /* Should not happen if track loaded correctly */
    }

    original_sflag_of_edited_sector = track->sflag[sector_idx];
    was_edited_sector_compressed = IMD_SDR_IS_COMPRESSED(original_sflag_of_edited_sector);

    /* Copy new data into the in-memory track buffer for the specified sector */
    memcpy(track->data + ((size_t)sector_idx * track->sector_size), buffer, track->sector_size);

    ImdWriteOpts write_opts;
    memcpy(&write_opts, &default_libimdf_write_opts, sizeof(ImdWriteOpts));

    /* Determine if this write forces the track to be uncompressed */
    if (was_edited_sector_compressed) {
        uint8_t fill_byte_check;
        if (!imd_is_uniform(buffer, track->sector_size, &fill_byte_check)) {
            /* The edited sector was compressed, but its new data is non-uniform.
             * The entire track must now be written uncompressed.
             */
            write_opts.compression_mode = IMD_COMPRESSION_FORCE_DECOMPRESS;
            track_rewritten_as_uncompressed = 1;
            DEBUG_PRINTF("LibIMDF: Sector C%u H%u S%u (Phys %d) edit makes it non-uniform. Track forced uncompressed.\n", cyl, head, logical_sector_id, sector_idx);
        }
        else {
            /* Edited sector was compressed, and new data is still uniform.
             * Use AS_READ, which will attempt to keep it compressed if underlying libimd supports it for this mode.
             */
            write_opts.compression_mode = IMD_COMPRESSION_AS_READ;
            DEBUG_PRINTF("LibIMDF: Sector C%u H%u S%u (Phys %d) was compressed, data still uniform. Using AS_READ.\n", cyl, head, logical_sector_id, sector_idx);
        }
    }
    else {
        /* Edited sector was not compressed. Use default compression mode for rewrite. */
        write_opts.compression_mode = IMD_COMPRESSION_AS_READ;
    }

    /* Rewrite the entire image file (rewrite_image_file calls imd_write_track_imd for each track) */
    rewrite_res = rewrite_image_file(imdf, track_idx, &write_opts);
    if (rewrite_res != IMDF_ERR_OK) {
        DEBUG_PRINTF("LibIMDF Write Sector: Failed to rewrite image file (%d). In-memory data was changed but not persisted fully.\n", rewrite_res);
        return rewrite_res;
    }

    /* After successful rewrite, update sflag(s) in libimdf's memory for the affected track */
    if (track_rewritten_as_uncompressed) {
        DEBUG_PRINTF("LibIMDF: Track C%u H%u rewritten uncompressed. Updating all in-memory sflags for this track.\n", track->cyl, track->head);
        for (uint8_t i = 0; i < track->num_sectors; ++i) {
            uint8_t current_sflag_val = track->sflag[i]; /* Get original DAM/ERR state for this sector i */
            /* Base type is now normal because the whole track was written uncompressed */

            /* Preserve DAM/ERR flags from the original sflag of sector i */
            if (IMD_SDR_HAS_DAM(current_sflag_val) && IMD_SDR_HAS_ERR(current_sflag_val)) {
                track->sflag[i] = IMD_SDR_DELETED_ERR;
            }
            else if (IMD_SDR_HAS_ERR(current_sflag_val)) {
                track->sflag[i] = IMD_SDR_NORMAL_ERR;
            }
            else if (IMD_SDR_HAS_DAM(current_sflag_val)) {
                track->sflag[i] = IMD_SDR_NORMAL_DAM;
            }
            else {
                track->sflag[i] = IMD_SDR_NORMAL;
            }
        }
    }
    else {
        /* Track was not globally forced uncompressed by this operation.
         * Update only the sflag for the specifically edited sector (sector_idx)
         * based on its new data and the write options used.
         */
        uint8_t new_predicted_sflag_for_edited_sector = 0;
        uint8_t fill_byte_dummy;
        /* Check the data that was actually written (now in track->data for the edited sector) */
        int is_edited_sector_data_uniform = imd_is_uniform(track->data + ((size_t)sector_idx * track->sector_size), track->sector_size, &fill_byte_dummy);

        /* Use the original sflag of the *edited sector* for DAM/ERR preservation */
        uint8_t final_dam = IMD_SDR_HAS_DAM(original_sflag_of_edited_sector) && !write_opts.force_non_deleted;
        uint8_t final_err = IMD_SDR_HAS_ERR(original_sflag_of_edited_sector) && !write_opts.force_non_bad;
        uint8_t base_sflag_type_for_edited_sector;

        /* Determine the base type based on write_opts and actual data post-edit for *this* sector */
        /* This logic should primarily reflect what imd_write_track_imd would have decided for this specific sector
           given the overall track compression mode.
        */
        switch (write_opts.compression_mode) {
        case IMD_COMPRESSION_FORCE_DECOMPRESS:
            /* This case implies track_rewritten_as_uncompressed was true and handled above.
             * If somehow reached here, it means normal.
             */
            base_sflag_type_for_edited_sector = IMD_SDR_NORMAL;
            break;
        case IMD_COMPRESSION_FORCE_COMPRESS:
            base_sflag_type_for_edited_sector = is_edited_sector_data_uniform ? IMD_SDR_COMPRESSED : IMD_SDR_NORMAL;
            break;
        case IMD_COMPRESSION_AS_READ:
        default:
            if (was_edited_sector_compressed) { /* If original sflag for this sector was compressed */
                base_sflag_type_for_edited_sector = is_edited_sector_data_uniform ? IMD_SDR_COMPRESSED : IMD_SDR_NORMAL;
            }
            else { /* Original sflag for this sector was normal */
                if (is_edited_sector_data_uniform && write_opts.compression_mode == IMD_COMPRESSION_AS_READ) {
                    base_sflag_type_for_edited_sector = IMD_SDR_COMPRESSED;
                }
                else {
                    base_sflag_type_for_edited_sector = IMD_SDR_NORMAL;
                }
            }
            break;
        }

        if (base_sflag_type_for_edited_sector == IMD_SDR_NORMAL) {
            if (final_dam && final_err) new_predicted_sflag_for_edited_sector = IMD_SDR_DELETED_ERR;
            else if (final_err) new_predicted_sflag_for_edited_sector = IMD_SDR_NORMAL_ERR;
            else if (final_dam) new_predicted_sflag_for_edited_sector = IMD_SDR_NORMAL_DAM;
            else new_predicted_sflag_for_edited_sector = IMD_SDR_NORMAL;
        }
        else { /* IMD_SDR_COMPRESSED */
            if (final_dam && final_err) new_predicted_sflag_for_edited_sector = IMD_SDR_COMPRESSED_DEL_ERR;
            else if (final_err) new_predicted_sflag_for_edited_sector = IMD_SDR_COMPRESSED_ERR;
            else if (final_dam) new_predicted_sflag_for_edited_sector = IMD_SDR_COMPRESSED_DAM;
            else new_predicted_sflag_for_edited_sector = IMD_SDR_COMPRESSED;
        }
        DEBUG_PRINTF("LibIMDF: Updating in-memory sflag for edited sector %d (Phys %d) from 0x%02X to 0x%02X\n",
            logical_sector_id, sector_idx, original_sflag_of_edited_sector, new_predicted_sflag_for_edited_sector);
        track->sflag[sector_idx] = new_predicted_sflag_for_edited_sector;
    }

    return IMDF_ERR_OK;
}

/* --- Track Writing --- */

int imdf_write_track(ImdImageFile* imdf,
    uint8_t cyl,
    uint8_t head,
    uint8_t num_sectors,
    uint32_t sector_size,
    uint8_t fill_byte,
    const uint8_t* smap,
    const uint8_t* cmap,
    const uint8_t* hmap)
{
    int track_idx_int;
    ImdTrackInfo* track_ptr = NULL;
    int rewrite_res;
    int result;
    int existing_track = 0;
    size_t insert_idx = 0;
    uint8_t sector_size_code;

    if (!imdf) return IMDF_ERR_INVALID_ARG;
    if (imdf->write_protected) return IMDF_ERR_WRITE_PROTECTED;

    if ((imdf->max_cyl != 0xFF && cyl > imdf->max_cyl) ||
        (imdf->max_head != 0xFF && head > imdf->max_head)) {
        DEBUG_PRINTF("Write Track Error: C%u/H%u exceeds geometry Cmax%u/Hmax%u\n", cyl, head, imdf->max_cyl, imdf->max_head);
        return IMDF_ERR_GEOMETRY;
    }
    /* If num_sectors > 0 and smap is NULL, then cmap and hmap must also be NULL */

    if (num_sectors > 0 && smap == NULL && (cmap != NULL || hmap != NULL)) {
        DEBUG_PRINTF("Write Track Error: Optional maps (cmap/hmap) require a non-NULL smap if num_sectors > 0.\n");
        return IMDF_ERR_INVALID_ARG;
    }

    if (get_sector_size_code(sector_size, &sector_size_code) != 0) {
        return IMDF_ERR_SECTOR_SIZE;
    }

    track_idx_int = find_track_index_internal(imdf, cyl, head);
    existing_track = (track_idx_int >= 0);

    if (existing_track) {
        insert_idx = (size_t)track_idx_int;
        DEBUG_PRINTF("Write Track: Overwriting existing track at index %zu (C%u H%u)\n", insert_idx, cyl, head);
        track_ptr = &imdf->tracks[insert_idx];
        imd_free_track_data(track_ptr);
        memset(track_ptr, 0, sizeof(ImdTrackInfo));
    }
    else {
        DEBUG_PRINTF("Write Track: Creating new track for C%u H%u\n", cyl, head);
        insert_idx = find_insertion_index(imdf, cyl, head);

        if (imdf->num_tracks >= imdf->track_capacity) {
            size_t new_capacity = (imdf->track_capacity == 0) ? IMDF_INITIAL_TRACK_CAPACITY : imdf->track_capacity * 2;
            if (new_capacity <= imdf->track_capacity) { result = IMDF_ERR_ALLOC; goto cleanup_inserterror; }
            ImdTrackInfo* new_tracks = (ImdTrackInfo*)realloc(imdf->tracks, new_capacity * sizeof(ImdTrackInfo));
            if (!new_tracks) { result = IMDF_ERR_ALLOC; goto cleanup_inserterror; }
            imdf->tracks = new_tracks;
            imdf->track_capacity = new_capacity;
            DEBUG_PRINTF("Write Track: Reallocated track array to %zu\n", new_capacity);
        }

        if (insert_idx < imdf->num_tracks) {
            memmove(&imdf->tracks[insert_idx + 1],
                &imdf->tracks[insert_idx],
                (imdf->num_tracks - insert_idx) * sizeof(ImdTrackInfo));
        }

        track_ptr = &imdf->tracks[insert_idx];
        memset(track_ptr, 0, sizeof(ImdTrackInfo));
        imdf->num_tracks++;
    }

    track_ptr->cyl = cyl;
    track_ptr->head = head;
    track_ptr->num_sectors = num_sectors;
    track_ptr->sector_size_code = sector_size_code;
    track_ptr->sector_size = sector_size;
    track_ptr->mode = IMD_MODE_MFM_250; /* Default to a common mode */
    track_ptr->loaded = 1;

    track_ptr->hflag = 0;
    if (num_sectors > 0) { /* Maps only relevant if sectors exist */
        if (cmap != NULL) { /* smap must be non-NULL here due to earlier check */
            track_ptr->hflag |= IMD_HFLAG_CMAP_PRES;
        }
        if (hmap != NULL) { /* smap must be non-NULL here due to earlier check */
            track_ptr->hflag |= IMD_HFLAG_HMAP_PRES;
        }
    }

    if (num_sectors > 0) {
        int alloc_res = imd_alloc_track_data(track_ptr);
        if (alloc_res != 0) {
            result = map_libimd_error(alloc_res);
            goto cleanup_inserterror;
        }
        memset(track_ptr->data, fill_byte, track_ptr->data_size);

        for (uint8_t i = 0; i < num_sectors; ++i) {
            track_ptr->sflag[i] = IMD_SDR_NORMAL; /* Will be re-evaluated by imd_write_track_imd based on data and opts */
            if (smap) {
                track_ptr->smap[i] = smap[i];
            }
            else {
                track_ptr->smap[i] = i + 1;
            }
        }
        if (cmap != NULL) {
            memcpy(track_ptr->cmap, cmap, num_sectors);
        }
        if (hmap != NULL) {
            memcpy(track_ptr->hmap, hmap, num_sectors);
        }
    }
    else {
        track_ptr->data = NULL;
        track_ptr->data_size = 0;
    }


    ImdWriteOpts write_opts;
    memcpy(&write_opts, &default_libimdf_write_opts, sizeof(ImdWriteOpts));
    write_opts.compression_mode = IMD_COMPRESSION_FORCE_COMPRESS; /* Try to compress if uniform */

    rewrite_res = rewrite_image_file(imdf, insert_idx, &write_opts);
    if (rewrite_res != IMDF_ERR_OK) {
        result = rewrite_res;
        goto cleanup_inserterror;
    }

    /* After successful rewrite, update the in-memory sflag for the written track */
    /* This is a prediction based on the fill_byte and FORCE_COMPRESS option */
    if (num_sectors > 0) {
        for (uint8_t i = 0; i < track_ptr->num_sectors; ++i) {
            /* Since we filled with a single byte and used FORCE_COMPRESS, */
            /* all sectors should become IMD_SDR_COMPRESSED. */
            /* This assumes no DAM or ERR flags were intended by this high-level write. */
            track_ptr->sflag[i] = IMD_SDR_COMPRESSED;
        }
    }


    return IMDF_ERR_OK;

cleanup_inserterror:
    DEBUG_PRINTF("Write Track: Cleaning up after error %d during %s track\n", result, existing_track ? "overwrite of" : "insertion of new");
    if (!existing_track && track_ptr == &imdf->tracks[insert_idx]) {
        imd_free_track_data(track_ptr);
        if (insert_idx < imdf->num_tracks - 1) {
            memmove(&imdf->tracks[insert_idx],
                &imdf->tracks[insert_idx + 1],
                (imdf->num_tracks - 1 - insert_idx) * sizeof(ImdTrackInfo));
        }
        imdf->num_tracks--;
    }
    else if (existing_track && track_ptr) {
        DEBUG_PRINTF("Write Track: Overwrite for C%u H%u failed. In-memory track may be inconsistent until next open.\n", cyl, head);
    }
    return result;
}

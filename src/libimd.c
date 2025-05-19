/*
 * ImageDisk Library (Cross-Platform.)
 *
 * www.github.com/hharte/libimd
 *
 * Copyright (c) 2025, Howard M. Harte
 *
 * Reference:
 * The original MS-DOS version is available from Dave's Old Computers:
 * http://dunfield.classiccmp.org/img/
 *
 */

#include "libimd.h"
#include <string.h> /* For memset, memcpy, strncpy, strcspn, sscanf */
#include <stdlib.h> /* For malloc, free, realloc */
#include <stdio.h>  /* For FILE, fread, fputc, etc. */
#include <errno.h>  /* For errno */
#include <time.h>   /* For time, localtime, strftime */
#include <ctype.h>  /* For isdigit, isalpha, tolower in imd_ctoh */

 /* Define to enable debug printf statements */
//#define DEBUG_LIBIMD    /* Uncomment for debug */

 /* --- Debug Macro --- */
#ifdef DEBUG_LIBIMD
/* Define DEBUG_PRINTF to expand to printf when DEBUG_LIBIMD is defined */
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
/* Define DEBUG_PRINTF to do nothing when DEBUG_LIBIMD is not defined */
#define DEBUG_PRINTF(...) do { } while (0)
#endif

/* --- Constants and Internal Data --- */

/* Sentinel value for invalid position in interleave functions */
#define LIBIMD_INVALID_SECTOR_POS 0xFF

/* Sector size lookup table (bytes = 128 << code) */
static const uint32_t SECTOR_SIZE_LOOKUP[] = {
    128, 256, 512, 1024, 2048, 4096, 8192
};
static const size_t SECTOR_SIZE_LOOKUP_COUNT = sizeof(SECTOR_SIZE_LOOKUP) / sizeof(SECTOR_SIZE_LOOKUP[0]);


/* --- Internal Helper Functions --- */

/**
 * @brief Reads a specified number of bytes from a file stream.
 * @param buffer Pointer to the buffer to read into.
 * @param size Number of bytes to read.
 * @param file File stream to read from.
 * @return Number of bytes successfully read. Returns value less than size on EOF or error.
 */
static size_t read_bytes(void* buffer, size_t size, FILE* file) {
    if (size == 0) return 0; /* Handle zero-size read */
    /* Clear errno before fread */
    errno = 0;
    size_t bytes_read = fread(buffer, 1, size, file);
    /* Caller must check feof/ferror if bytes_read < size */
    return bytes_read;
}

/**
 * @brief Reads a single byte from a file stream.
 * @param file File stream to read from.
 * @param byte_out Pointer to store the read byte.
 * @return 1 on success, 0 on EOF, IMD_ERR_READ_ERROR on error.
 */
static int read_byte(FILE* file, uint8_t* byte_out) {
    errno = 0;
    int result = fgetc(file);
    if (result == EOF) {
        if (ferror(file)) {
            DEBUG_PRINTF("DEBUG: read_byte: fgetc returned EOF, ferror set (errno=%d). Returning IMD_ERR_READ_ERROR.\n", errno);
            return IMD_ERR_READ_ERROR; /* Error takes precedence */
        }
        else {
            DEBUG_PRINTF("DEBUG: read_byte: fgetc returned EOF, ferror NOT set (feof=%d). Returning 0.\n", feof(file));
            return 0; /* EOF */
        }
    }
    *byte_out = (uint8_t)result;
    return 1; /* Success */
}


/**
 * @brief Writes a specified number of bytes to a file stream.
 * @param buffer Pointer to the buffer to write from.
 * @param size Number of bytes to write.
 * @param file File stream to write to.
 * @return 0 on success, IMD_ERR_WRITE_ERROR on error.
 */
static int write_bytes(const void* buffer, size_t size, FILE* file) {
    if (size == 0) return 0;
    if (fwrite(buffer, 1, size, file) != size) {
        /* DEBUG_PRINTF can be added here if write failures need investigation */
        return IMD_ERR_WRITE_ERROR;
    }
    return 0;
}


/* --- Public Function Implementations --- */

/* --- Header and Comment Handling --- */

int imd_read_file_header(FILE* fimd, ImdHeaderInfo* header_info, char* header_line_buf, size_t buf_size) {
    char line[LIBIMD_MAX_HEADER_LINE];

    if (!fimd) return IMD_ERR_INVALID_ARG; /* Use specific code */
    clearerr(fimd); /* Clear status before read */
    if (fgets(line, sizeof(line), fimd) == NULL) {
        /*
         * Check for error vs EOF.
         * fgets returns NULL on error or EOF. The comment indicates
         * EOF should be treated as a read error in this context.
         * We check ferror to see if a specific error occurred, but
         * ultimately return IMD_ERR_READ_ERROR in either case (EOF or error)
         * according to the requirement. Directly returning the error code
         * avoids the unused result warning for ferror.
         */
        DEBUG_PRINTF("DEBUG: imd_read_file_header: fgets returned NULL (ferror=%d, feof=%d). Returning IMD_ERR_READ_ERROR.\n", ferror(fimd), feof(fimd));
        (void)ferror(fimd); /* Acknowledge check, but ignore result as per comment */
        return IMD_ERR_READ_ERROR;
    }

    /* Store raw line if buffer provided */
    if (header_line_buf && buf_size > 0) {
        snprintf(header_line_buf, buf_size, "%s", line);
    }

    /* Remove trailing newline characters */
    line[strcspn(line, "\r\n")] = 0;

    /* Basic validation */
    if (strncmp(line, "IMD ", 4) != 0) {
        DEBUG_PRINTF("DEBUG: imd_read_file_header: Header prefix 'IMD ' not found. Returning IMD_ERR_READ_ERROR.\n");
        return IMD_ERR_READ_ERROR; /* Treat as read error (invalid format) */
    }

    /* Parse if requested */
    if (header_info) {
        int fields;
        /* Initialize struct to ensure fields are zero if sscanf fails */
        memset(header_info, 0, sizeof(ImdHeaderInfo));

        /* Example format: IMD 1.18: 25/04/2024 15:30:00 */
        /* Note: %31[^:] reads up to 31 chars or until ':' */
        fields = sscanf(line, "IMD %31[^:]: %d/%d/%d %d:%d:%d",
            header_info->version,
            &header_info->day, &header_info->month, &header_info->year,
            &header_info->hour, &header_info->minute, &header_info->second);

        /* Check if parsing failed completely or partially */
        if (fields < 7) {
            DEBUG_PRINTF("DEBUG: imd_read_file_header: sscanf parsed only %d fields (expected 7). Date/time may be invalid.\n", fields);
            /* Parsing failed to get all date/time fields. */
            /* Check if at least the version string was parsed. */
            if (sscanf(line, "IMD %31[^:]:", header_info->version) != 1) {
                /* Version string itself couldn't be parsed, set to "Unknown" */
                DEBUG_PRINTF("DEBUG: imd_read_file_header: Failed to parse version string. Setting to 'Unknown'.\n");
                snprintf(header_info->version, sizeof(header_info->version), "Unknown");
                header_info->version[sizeof(header_info->version) - 1] = '\0'; /* Ensure null termination */
            }
            /* Ensure date/time fields are zero since parsing was incomplete */
            header_info->day = header_info->month = header_info->year = 0;
            header_info->hour = header_info->minute = header_info->second = 0;
        }
        else {
            /* All 7 fields were assigned by sscanf. Now validate date/time ranges. */
            if (header_info->month < 1 || header_info->month > 12 ||
                header_info->day < 1 || header_info->day > 31 || /* Simplistic day check */
                header_info->hour < 0 || header_info->hour > 23 ||
                header_info->minute < 0 || header_info->minute > 59 ||
                header_info->second < 0 || header_info->second > 59)
            {
                /* Values are out of range, treat as parse failure for date/time */
                DEBUG_PRINTF("DEBUG: imd_read_file_header: Parsed date/time values out of range. Zeroing fields.\n");
                header_info->day = header_info->month = header_info->year = 0;
                header_info->hour = header_info->minute = header_info->second = 0;
            }
        }
    }

    return 0; /* Success (even if parsing failed, the header line was read) */
}

/* Modified imd_read_comment_block: Update size only on success */
char* imd_read_comment_block(FILE* fimd, size_t* comment_size_out) {
    char* buffer = NULL;
    size_t size = 0;
    size_t capacity = 1024; /* Initial capacity */
    int c;

    /* Check arguments first */
    if (!fimd || !comment_size_out) { /* Check both */
        /* No easy way to return IMD_ERR_INVALID_ARG as return type is char* */
        DEBUG_PRINTF("ERROR: imd_read_comment_block: Invalid argument (fimd or comment_size_out is NULL).\n");
        if (comment_size_out) *comment_size_out = 0; /* Set size to 0 on error */
        return NULL;
    }
    *comment_size_out = 0; /* Initialize output size to 0 */

    buffer = (char*)malloc(capacity);
    if (!buffer) {
        /* No easy way to return IMD_ERR_ALLOC */
        DEBUG_PRINTF("ERROR: imd_read_comment_block: Initial malloc failed (capacity=%zu).\n", capacity);
        return NULL;
    }

    clearerr(fimd); /* Clear status before reading loop */
    while ((c = fgetc(fimd)) != EOF && c != LIBIMD_COMMENT_EOF_MARKER) {
        if (size >= capacity - 1) { /* Need space for char + null terminator */
            size_t new_capacity = capacity * 2;
            /* Add arbitrary upper limit if desired */
            /* if (new_capacity > MAX_COMMENT_SIZE) new_capacity = MAX_COMMENT_SIZE; */
            if (new_capacity <= capacity) { /* Check for size_t overflow */
                DEBUG_PRINTF("ERROR: imd_read_comment_block: Capacity overflow during resize (capacity=%zu).\n", capacity);
                free(buffer);
                return NULL;
            }

            DEBUG_PRINTF("DEBUG: imd_read_comment_block: Resizing comment buffer from %zu to %zu\n", capacity, new_capacity);
            char* new_buffer = (char*)realloc(buffer, new_capacity);
            if (!new_buffer) {
                DEBUG_PRINTF("ERROR: imd_read_comment_block: realloc failed trying to resize to %zu bytes.\n", new_capacity);
                perror("realloc"); /* Print system error */
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }
        buffer[size++] = (char)c;
    }

    if (c == EOF) { /* Didn't find marker */
        int eof_flag = feof(fimd);
        int error_flag = ferror(fimd);
        (void)eof_flag; (void)error_flag; /* Unused unless DEBUG_PRINTF() is defined. */
        DEBUG_PRINTF("ERROR: imd_read_comment_block: EOF encountered before marker (EOF=%d, Error=%d, size read=%zu).\n", eof_flag, error_flag, size);
        free(buffer);
        return NULL; /* Return NULL without updating *comment_size_out */
    }

    /* Found marker (c == LIBIMD_COMMENT_EOF_MARKER) */
    buffer[size] = '\0'; /* Null-terminate */

    /* Update size ONLY on success */
    *comment_size_out = size;

    /* Shrink buffer to actual size + 1 for null terminator */
    /* Note: If realloc fails here, we still return the original, larger buffer */
    char* final_buffer = (char*)realloc(buffer, size + 1);
    if (!final_buffer && size > 0) { /* Only warn if realloc fails and size > 0 */
        /* Use DEBUG_PRINTF for non-critical warning */
        DEBUG_PRINTF("WARNING: imd_read_comment_block: Final realloc (shrink to %zu) failed, returning potentially larger buffer.\n", size + 1);
        return buffer; /* Return original potentially oversized buffer */
    }
    else if (!final_buffer && size == 0) {
        /* If size is 0 and realloc fails, original buffer (size 1) is still valid */
        return buffer;
    }


    return final_buffer; /* Return shrunken buffer or original if size was 0 */
}


int imd_skip_comment_block(FILE* fimd) {
    int c;
    if (!fimd) return IMD_ERR_INVALID_ARG;
    clearerr(fimd); /* Clear status before read loop */
    while ((c = fgetc(fimd)) != EOF && c != LIBIMD_COMMENT_EOF_MARKER);
    if (c == EOF) {
        /*
        * If c is EOF, it means the loop terminated without finding
        * the LIBIMD_COMMENT_EOF_MARKER. This is considered an error.
        */
        DEBUG_PRINTF("DEBUG: imd_skip_comment_block: EOF encountered before marker (ferror=%d, feof=%d). Returning IMD_ERR_READ_ERROR.\n", ferror(fimd), feof(fimd));
        (void)ferror(fimd); /* Acknowledge check */
        return IMD_ERR_READ_ERROR;
    }
    return 0; /* Success */
}

int imd_write_file_header(FILE* fout, const char* version_string) {
    if (!fout || !version_string) return IMD_ERR_INVALID_ARG;

    time_t now = time(NULL);
    struct tm* tminfo = localtime(&now);
    char timestamp[20]; /* DD/MM/YYYY HH:MM:SS */

    /* Check if localtime returned NULL */
    if (tminfo == NULL) {
        DEBUG_PRINTF("DEBUG: imd_write_file_header: localtime() returned NULL. Returning IMD_ERR_WRITE_ERROR.\n");
        return IMD_ERR_WRITE_ERROR; /* Indicate error occurred */
    }

    if (strftime(timestamp, sizeof(timestamp), "%d/%m/%Y %H:%M:%S", tminfo) == 0) {
        DEBUG_PRINTF("DEBUG: imd_write_file_header: strftime() failed. Returning IMD_ERR_WRITE_ERROR.\n");
        return IMD_ERR_WRITE_ERROR; /* Error formatting time */
    }

    if (fprintf(fout, "IMD %s: %s\r\n", version_string, timestamp) < 0) {
        DEBUG_PRINTF("DEBUG: imd_write_file_header: fprintf failed. Returning IMD_ERR_WRITE_ERROR.\n");
        return IMD_ERR_WRITE_ERROR; /* Write error */
    }
    return 0;
}

int imd_write_comment_block(FILE* fout, const char* comment, size_t comment_len) {
    if (!fout) return IMD_ERR_INVALID_ARG;

    if (comment && comment_len > 0) {
        if (write_bytes(comment, comment_len, fout) != 0) {
            DEBUG_PRINTF("DEBUG: imd_write_comment_block: write_bytes failed for comment. Returning IMD_ERR_WRITE_ERROR.\n");
            return IMD_ERR_WRITE_ERROR; /* Error writing comment */
        }
    }
    if (fputc(LIBIMD_COMMENT_EOF_MARKER, fout) == EOF) {
        DEBUG_PRINTF("DEBUG: imd_write_comment_block: fputc failed for marker. Returning IMD_ERR_WRITE_ERROR.\n");
        return IMD_ERR_WRITE_ERROR; /* Error writing terminator */
    }
    return 0;
}

/* --- Track Handling --- */

/* Function: imd_get_sector_size */
uint32_t imd_get_sector_size(const ImdTrackInfo* track) {
    if (!track) {
        /* DEBUG_PRINTF("DEBUG: imd_get_sector_size: track pointer is NULL.\n"); */ /* Optional debug */
        return 0; /* Invalid argument */
    }
    if (track->sector_size_code >= SECTOR_SIZE_LOOKUP_COUNT) {
        DEBUG_PRINTF("DEBUG: imd_get_sector_size: Invalid sector_size_code (%u >= %zu).\n", track->sector_size_code, SECTOR_SIZE_LOOKUP_COUNT);
        return 0; /* Invalid code */
    }
    /* Return size directly from lookup table */
    return SECTOR_SIZE_LOOKUP[track->sector_size_code];
}

/* Function: imd_alloc_track_data */
int imd_alloc_track_data(ImdTrackInfo* track) {
    size_t total_data_size;
    uint32_t sector_size; /* Use uint32_t to match function return */

    if (!track) {
        return IMD_ERR_INVALID_ARG;
    }

    /* If data already exists, return error. */
    if (track->data != NULL) {
        DEBUG_PRINTF("DEBUG: imd_alloc_track_data: Error - Track data already allocated.\n");
        return IMD_ERR_ALLOC; /* Or a more specific error? Using ALLOC for now */
    }


    /* Calculate required size using the public getter */
    sector_size = imd_get_sector_size(track);
    if (sector_size == 0) {
        DEBUG_PRINTF("DEBUG: imd_alloc_track_data: Error - Invalid sector size derived from code %u.\n", track->sector_size_code);
        return IMD_ERR_INVALID_ARG; /* Invalid sector size code */
    }

    /* ***** FIX START ***** */
    /* Store the calculated sector size back into the track structure */
    track->sector_size = sector_size;
    /* ***** FIX END ***** */

    if (track->num_sectors == 0) {
        DEBUG_PRINTF("DEBUG: imd_alloc_track_data: Zero sectors requested, setting data=NULL, size=0.\n");
        track->data = NULL;
        track->data_size = 0;
        return 0; /* Nothing to allocate for 0 sectors */
    }

    total_data_size = (size_t)track->num_sectors * sector_size;

    /* Check for multiplication overflow */
    if (total_data_size / sector_size != track->num_sectors) {
        DEBUG_PRINTF("DEBUG: imd_alloc_track_data: Error - Data size overflow (%u * %u).\n", track->num_sectors, sector_size);
        return IMD_ERR_ALLOC; /* Overflow */
    }

    /* Allocate */
    track->data = (uint8_t*)malloc(total_data_size);
    if (!track->data) {
        DEBUG_PRINTF("DEBUG: imd_alloc_track_data: Error - malloc failed for %zu bytes.\n", total_data_size);
        track->data_size = 0;
        return IMD_ERR_ALLOC; /* Allocation failure */
    }

    track->data_size = total_data_size;
    DEBUG_PRINTF("DEBUG: imd_alloc_track_data: Successfully allocated %zu bytes for %u sectors of size %u.\n",
        total_data_size, track->num_sectors, sector_size);
    return 0; /* Success */
}


const uint32_t* imd_get_sector_size_lookup(size_t* count) {
    if (count) {
        *count = SECTOR_SIZE_LOOKUP_COUNT;
    }
    return SECTOR_SIZE_LOOKUP;
}

void imd_free_track_data(ImdTrackInfo* track) {
    if (track && track->data) {
        DEBUG_PRINTF("DEBUG: imd_free_track_data: Freeing data buffer for track C%u H%u.\n", track->cyl, track->head);
        free(track->data);
        track->data = NULL;
        track->data_size = 0;
        track->loaded = 0; /* Reset loaded flag when freeing */
    }
}

/* Modified imd_load_track to use new IMD_SDR_* defines */
int imd_load_track(FILE* fimd, ImdTrackInfo* track, uint8_t fill_byte) {
    uint8_t head_byte;
    size_t current_offset = 0;
    int read_status;
    long start_pos;

    if (!fimd || !track) { /* Check both */
        return IMD_ERR_INVALID_ARG;
    }

    start_pos = ftell(fimd); /* Remember start position for error recovery */
    if (start_pos < 0) {
        DEBUG_PRINTF("DEBUG: imd_load_track: ftell failed before reading track. Returning IMD_ERR_SEEK_ERROR.\n");
        return IMD_ERR_SEEK_ERROR; /* Use specific error */
    }

    memset(track, 0, sizeof(ImdTrackInfo)); /* Initialize track structure */

    /* Read track header */
    clearerr(fimd); /* Clear status before reading header */
    read_status = read_byte(fimd, &track->mode);
    if (read_status <= 0) { /* EOF or error before first byte */
        DEBUG_PRINTF("DEBUG: imd_load_track: EOF or Error (%d) reading first track byte. Pos=%ld.\n", read_status, start_pos);
        if (read_status < 0) {
            /* Attempt to seek back only if an error occurred */
            if (fseek(fimd, start_pos, SEEK_SET) != 0) {
                DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after read error.\n");
                /* Can't do much more, error already occurred */
            }
            return IMD_ERR_READ_ERROR; /* Return specific error */
        }
        return 0; /* Return 0 for clean EOF */
    }
    /* If we read the first byte, subsequent non-success (EOF or error) is a failure */
    if (read_byte(fimd, &track->cyl) != 1 ||
        read_byte(fimd, &head_byte) != 1 ||
        read_byte(fimd, &track->num_sectors) != 1 ||
        read_byte(fimd, &track->sector_size_code) != 1)
    {
        DEBUG_PRINTF("DEBUG: imd_load_track: Error/EOF reading track header fields.\n");
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back on error */
            DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after header read error.\n");
        }
        return IMD_ERR_READ_ERROR; /* Treat as read error */
    }


    /* Decode header fields */
    track->head = head_byte & IMD_HFLAG_HEAD_MASK;
    track->hflag = head_byte & IMD_HFLAG_MASK;
    /* Validate critical header fields */
    if (track->mode >= LIBIMD_NUM_MODES || track->head > 1 || track->sector_size_code >= SECTOR_SIZE_LOOKUP_COUNT) {
        DEBUG_PRINTF("DEBUG: imd_load_track: Invalid header field (mode=%u, head=%u, size_code=%u). Returning IMD_ERR_READ_ERROR.\n", track->mode, track->head, track->sector_size_code);
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after invalid header.\n");
        }
        return IMD_ERR_READ_ERROR; /* Invalid format */
    }
    /* Use direct lookup */
    track->sector_size = SECTOR_SIZE_LOOKUP[track->sector_size_code];
    if (track->sector_size == 0) { /* Should not happen if code is valid */
        DEBUG_PRINTF("DEBUG: imd_load_track: Invalid sector size derived from code %u. Returning IMD_ERR_READ_ERROR.\n", track->sector_size_code);
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after invalid sector size.\n");
        }
        return IMD_ERR_READ_ERROR;
    }

    /* Validate num_sectors against buffer limits after it's read and before it's used as a size. */
    if (track->num_sectors > LIBIMD_MAX_SECTORS_PER_TRACK) {
        DEBUG_PRINTF("DEBUG: imd_load_track: Invalid num_sectors (%u > %d). Returning IMD_ERR_READ_ERROR.\n", track->num_sectors, LIBIMD_MAX_SECTORS_PER_TRACK);
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after invalid num_sectors.\n");
        }
        return IMD_ERR_READ_ERROR; /* Invalid format: num_sectors exceeds array bounds */
    }

    /* Read optional maps */
    if (track->num_sectors > 0) {
        if (read_bytes(track->smap, track->num_sectors, fimd) != track->num_sectors) {
            DEBUG_PRINTF("DEBUG: imd_load_track: Error/EOF reading Smap. Returning IMD_ERR_READ_ERROR.\n");
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after smap read error.\n");
            }
            return IMD_ERR_READ_ERROR;
        }
        if (track->hflag & IMD_HFLAG_CMAP_PRES) { /* Cylinder Map */
            if (read_bytes(track->cmap, track->num_sectors, fimd) != track->num_sectors) {
                DEBUG_PRINTF("DEBUG: imd_load_track: Error/EOF reading Cmap. Returning IMD_ERR_READ_ERROR.\n");
                if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                    DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after cmap read error.\n");
                }
                return IMD_ERR_READ_ERROR;
            }
        }
        else {
            /* If Cmap not present, fill it with the track's cylinder number */
            memset(track->cmap, track->cyl, track->num_sectors);
        }
        if (track->hflag & IMD_HFLAG_HMAP_PRES) { /* Head Map */
            if (read_bytes(track->hmap, track->num_sectors, fimd) != track->num_sectors) {
                DEBUG_PRINTF("DEBUG: imd_load_track: Error/EOF reading Hmap. Returning IMD_ERR_READ_ERROR.\n");
                if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                    DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after hmap read error.\n");
                }
                return IMD_ERR_READ_ERROR;
            }
        }
        else {
            /* If Hmap not present, fill it with the track's head number */
            memset(track->hmap, track->head, track->num_sectors);
        }
    }
    /* Call imd_alloc_track_data */
    int alloc_status = imd_alloc_track_data(track);
    if (alloc_status != 0) {
        DEBUG_PRINTF("DEBUG: imd_load_track: Allocation failed via imd_alloc_track_data (status %d).\n", alloc_status);
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after alloc failure.\n");
        }
        return alloc_status; /* Return allocation error */
    }
    /* Handle the case where num_sectors was 0 (handled by imd_alloc_track_data) */
    if (track->num_sectors == 0) {
        track->loaded = 1; /* Consider loaded even with 0 sectors */
        DEBUG_PRINTF("DEBUG: imd_load_track: Zero sectors. Returning 1 (Success).\n");
        return 1; /* Valid track with 0 sectors */
    }

    /* Check if allocation resulted in data buffer (should have if num_sectors > 0) */
    if (!track->data) {
        /* This should theoretically not happen if alloc_status was 0 and num_sectors > 0 */
        DEBUG_PRINTF("ERROR: imd_load_track: Allocation reported success but data is NULL.\n");
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after inconsistent alloc state.\n");
        }
        return IMD_ERR_ALLOC; /* Should be alloc error */
    }

    /* Read sector data */
    for (uint8_t i = 0; i < track->num_sectors; ++i) {
        uint8_t sector_type;
        read_status = read_byte(fimd, &sector_type);
        if (read_status != 1) { /* Check for success (1), handle EOF/Error */
            DEBUG_PRINTF("DEBUG: imd_load_track: Error/EOF reading sector flag %u. Returning IMD_ERR_READ_ERROR.\n", i);
            imd_free_track_data(track); /* Use free function */
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after sector flag read error.\n");
            }
            return IMD_ERR_READ_ERROR;
        }
        track->sflag[i] = sector_type; /* Store the raw SDR byte */
        uint8_t* sector_ptr = track->data + current_offset;

        /* Check if sector has data to read */
        if (IMD_SDR_HAS_DATA(sector_type)) {
            if (IMD_SDR_IS_COMPRESSED(sector_type)) {
                /* Compressed: read 1 fill byte */
                uint8_t fill_value;
                int fill_read_status = read_byte(fimd, &fill_value);
                if (fill_read_status != 1) { /* Error or EOF reading fill byte */
                    DEBUG_PRINTF("DEBUG: imd_load_track: Error/EOF reading fill byte for sector %u. Returning IMD_ERR_READ_ERROR.\n", i);
                    imd_free_track_data(track);
                    if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                        DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after fill byte read error.\n");
                    }
                    return IMD_ERR_READ_ERROR;
                }
                memset(sector_ptr, fill_value, track->sector_size);
            }
            else {
                /* Normal: read sector_size bytes */
                if (track->sector_size > LIBIMD_MAX_SECTOR_SIZE) { /* Should be caught by size check before */
                    DEBUG_PRINTF("ERROR: imd_load_track: Invalid sector size %u > MAX %d. Returning IMD_ERR_READ_ERROR.\n", track->sector_size, LIBIMD_MAX_SECTOR_SIZE);
                    imd_free_track_data(track);
                    if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                        DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after invalid sector size check.\n");
                    }
                    return IMD_ERR_READ_ERROR;
                }
                if (read_bytes(sector_ptr, track->sector_size, fimd) != track->sector_size) {
                    /* Error or EOF during data read - indicates truncated file or read error */
                    DEBUG_PRINTF("DEBUG: imd_load_track: Error/EOF reading sector %u data. Returning IMD_ERR_READ_ERROR.\n", i);
                    imd_free_track_data(track);
                    if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                        DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after sector data read error.\n");
                    }
                    return IMD_ERR_READ_ERROR;
                }
            }
        }
        else if (sector_type == IMD_SDR_UNAVAILABLE) {
            /* Unavailable: No data follows, fill buffer */
            memset(sector_ptr, fill_byte, track->sector_size);
        }
        else {
            /* Error: Unknown/Invalid Sector Data Record type */
            DEBUG_PRINTF("ERROR: imd_load_track: Unknown Sector Data Record type 0x%02X for sector %u. Returning IMD_ERR_READ_ERROR\n", sector_type, i);
            imd_free_track_data(track);
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                DEBUG_PRINTF("DEBUG: imd_load_track: fseek back failed after unknown SDR type.\n");
            }
            return IMD_ERR_READ_ERROR;
        }
        /* Advance offset for next sector's data */
        current_offset += track->sector_size;
    }
    track->loaded = 1;
    DEBUG_PRINTF("DEBUG: imd_load_track: Success for C%u H%u. Returning 1.\n", track->cyl, track->head);
    return 1; /* Success */
}

/* Helper to skip sector data based on flag */
/* Returns 0 on success, negative IMD_ERR_* on failure */
static int skip_sector_data(FILE* fimd, uint8_t sector_flag, uint32_t sector_size, long track_start_pos) {
    long data_to_skip = 0;
    int skip_result = IMD_ERR_READ_ERROR; /* Default to generic error */

    DEBUG_PRINTF("DEBUG:   skip_sector_data: flag=0x%02X, size=%u\n", sector_flag, sector_size);

    /* Determine amount of data to skip based on sector record type */
    if (sector_flag == IMD_SDR_UNAVAILABLE) {
        data_to_skip = 0; /* No data follows */
    }
    else if (IMD_SDR_IS_COMPRESSED(sector_flag)) { /* Handles 0x02, 0x04, 0x06, 0x08 */
        data_to_skip = 1; /* Skip the single fill byte */
    }
    else if (sector_flag == IMD_SDR_NORMAL || sector_flag == IMD_SDR_NORMAL_DAM ||
        sector_flag == IMD_SDR_NORMAL_ERR || sector_flag == IMD_SDR_DELETED_ERR) {
        /* Handles 0x01, 0x03, 0x05, 0x07 */
        data_to_skip = (long)sector_size; /* Skip the full sector data */
    }
    else {
        /* Error: Unrecognized sector record type */
        DEBUG_PRINTF("ERROR:   skip_sector_data: Invalid sector flag 0x%02X\n", sector_flag);
        if (fseek(fimd, track_start_pos, SEEK_SET) != 0) { /* Attempt to restore position */
            DEBUG_PRINTF("DEBUG:   skip_sector_data: fseek back failed after invalid flag.\n");
        }
        return IMD_ERR_READ_ERROR; /* Indicate invalid format error */
    }


    DEBUG_PRINTF("DEBUG:   skip_sector_data: calculated data_to_skip = %ld\n", data_to_skip);

    if (data_to_skip == 0) {
        return 0; /* Nothing to skip, success */
    }

    /* Try fseek first */
    clearerr(fimd);
    long pos_before_seek = ftell(fimd);
    if (pos_before_seek < 0) { /* Check ftell result */
        DEBUG_PRINTF("ERROR:   skip_sector_data: ftell failed before seek.\n");
        /* Cannot reliably seek back, return seek error */
        return IMD_ERR_SEEK_ERROR;
    }

    if (fseek(fimd, data_to_skip, SEEK_CUR) == 0) {
        /* fseek reported success, check position */
        long pos_after_seek = ftell(fimd);
        if (pos_after_seek < 0) { /* Check ftell result after seek */
            DEBUG_PRINTF("ERROR:   skip_sector_data: ftell failed after seek.\n");
            if (fseek(fimd, track_start_pos, SEEK_SET) != 0) { /* Restore pos */
                DEBUG_PRINTF("DEBUG:   skip_sector_data: fseek back failed after ftell error.\n");
            }
            skip_result = IMD_ERR_SEEK_ERROR;
        }
        else if (pos_after_seek == pos_before_seek + data_to_skip) {
            /* Position matches exactly, seek was truly successful */
            DEBUG_PRINTF("DEBUG:   skip_sector_data: fseek successful and position verified.\n");
            skip_result = 0; /* Success! */
        }
        else {
            /* fseek reported success, but position mismatch. This often indicates */
            /* seeking past EOF on some systems/streams. Treat as error. */
            DEBUG_PRINTF("WARNING: skip_sector_data: fseek successful but position mismatch (%ld vs %ld + %ld = %ld). Likely EOF.\n",
                pos_after_seek, pos_before_seek, data_to_skip, pos_before_seek + data_to_skip);
            if (fseek(fimd, track_start_pos, SEEK_SET) != 0) { /* Restore pos */
                DEBUG_PRINTF("DEBUG:   skip_sector_data: fseek back failed after position mismatch.\n");
            }
            skip_result = IMD_ERR_READ_ERROR; /* Treat position mismatch as read error (EOF) */
        }
    }
    else {
        /* fseek failed, try reading and discarding */
        DEBUG_PRINTF("DEBUG:   skip_sector_data: fseek failed (errno=%d). Falling back to read.\n", errno);
        clearerr(fimd); /* Clear stream errors before trying fread */
        /* Ensure position is restored before attempting read fallback */
        if (fseek(fimd, pos_before_seek, SEEK_SET) != 0) {
            DEBUG_PRINTF("ERROR:   skip_sector_data: Failed to restore position before read fallback.\n");
            /* Attempt track start restore */
            if (fseek(fimd, track_start_pos, SEEK_SET) != 0) {
                DEBUG_PRINTF("DEBUG:   skip_sector_data: fseek back to track start also failed.\n");
            }
            return IMD_ERR_SEEK_ERROR;
        }

        uint8_t skip_buf[512];
        long remaining_skip = data_to_skip;
        while (remaining_skip > 0) {
            size_t read_amount = (remaining_skip > (long)sizeof(skip_buf)) ? sizeof(skip_buf) : (size_t)remaining_skip;
            DEBUG_PRINTF("DEBUG:   skip_sector_data: Fallback read: trying to read %zu bytes\n", read_amount);
            size_t bytes_actually_read = fread(skip_buf, 1, read_amount, fimd);
            DEBUG_PRINTF("DEBUG:   skip_sector_data: Fallback read: actually read %zu bytes\n", bytes_actually_read);

            if (bytes_actually_read < read_amount) {
                /* Check if it was due to error or EOF */
                if (ferror(fimd)) {
                    DEBUG_PRINTF("ERROR:   skip_sector_data: Fallback read ERROR (ferror set, errno=%d)\n", errno);
                    skip_result = IMD_ERR_READ_ERROR;
                }
                else { /* Must be EOF if not error */
                    DEBUG_PRINTF("ERROR:   skip_sector_data: Fallback read ERROR (EOF reached mid-skip)\n");
                    skip_result = IMD_ERR_READ_ERROR; /* EOF during data read is an error */
                }
                if (fseek(fimd, track_start_pos, SEEK_SET) != 0) { /* Restore pos */
                    DEBUG_PRINTF("DEBUG:   skip_sector_data: fseek back failed after read error.\n");
                }
                goto end_skip; /* Exit while loop and function */
            }
            remaining_skip -= (long)bytes_actually_read;
            DEBUG_PRINTF("DEBUG:   skip_sector_data: Fallback read: remaining_skip = %ld\n", remaining_skip);
        }
        /* If loop completed, skip was successful */
        DEBUG_PRINTF("DEBUG:   skip_sector_data: Fallback read finished successfully.\n");
        skip_result = 0; /* Success */
    }

end_skip:
    return skip_result;
}


/* Reads header/maps only, skips data */
int imd_read_track_header(FILE* fimd, ImdTrackInfo* track) {
    uint8_t head_byte;
    int read_status;
    long start_pos;
    uint8_t raw_mode, raw_cyl, raw_nsec, raw_ssize; /* For debug */

    if (!fimd || !track) { /* Combined check */
        DEBUG_PRINTF("DEBUG: imd_read_track_header: Invalid argument (NULL). Returning IMD_ERR_INVALID_ARG.\n");
        return IMD_ERR_INVALID_ARG; /* Invalid arguments */
    }

    start_pos = ftell(fimd); /* Remember start position for error recovery */
    if (start_pos < 0) {
        DEBUG_PRINTF("DEBUG: imd_read_track_header: ftell failed. Returning IMD_ERR_SEEK_ERROR.\n");
        return IMD_ERR_SEEK_ERROR;
    }

    memset(track, 0, sizeof(ImdTrackInfo)); /* Initialize track structure */

    /* Read track header bytes */
    clearerr(fimd); /* Clear status before reading header */
    read_status = read_byte(fimd, &raw_mode);
    if (read_status <= 0) { /* EOF or error before first byte */
        DEBUG_PRINTF("DEBUG: imd_read_track_header: EOF or Error (%d) reading first byte. Pos=%ld.\n", read_status, start_pos);
        if (read_status < 0) {
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back only on error */
                DEBUG_PRINTF("DEBUG: imd_read_track_header: fseek back failed after read error.\n");
            }
            return IMD_ERR_READ_ERROR; /* Return specific error */
        }
        return 0; /* Return 0 for clean EOF */
    }
    /* If we read the first byte, subsequent non-success (EOF or error) is a failure */
    if (read_byte(fimd, &raw_cyl) != 1 ||
        read_byte(fimd, &head_byte) != 1 ||
        read_byte(fimd, &raw_nsec) != 1 ||
        read_byte(fimd, &raw_ssize) != 1)
    {
        DEBUG_PRINTF("DEBUG: imd_read_track_header: Error/EOF reading track header fields. Returning IMD_ERR_READ_ERROR.\n");
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back on error */
            DEBUG_PRINTF("DEBUG: imd_read_track_header: fseek back failed after header read error.\n");
        }
        return IMD_ERR_READ_ERROR;
    }

    DEBUG_PRINTF("DEBUG: imd_read_track_header: Raw Header Bytes: Mode=0x%02X, Cyl=0x%02X, HeadByte=0x%02X, Nsec=0x%02X, SSize=0x%02X\n",
        raw_mode, raw_cyl, head_byte, raw_nsec, raw_ssize);


    /* Assign and Decode header fields */
    track->mode = raw_mode;
    track->cyl = raw_cyl;
    track->num_sectors = raw_nsec;
    track->sector_size_code = raw_ssize;
    track->head = head_byte & IMD_HFLAG_HEAD_MASK;
    track->hflag = head_byte & IMD_HFLAG_MASK;

    /* Validate decoded fields */
    if (track->mode >= LIBIMD_NUM_MODES || track->head > 1 || track->sector_size_code >= SECTOR_SIZE_LOOKUP_COUNT) {
        DEBUG_PRINTF("ERROR: imd_read_track_header: Invalid decoded header field (mode=%u, head=%u, size_code=%u). Returning IMD_ERR_READ_ERROR.\n", track->mode, track->head, track->sector_size_code);
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_read_track_header: fseek back failed after invalid header.\n");
        }
        return IMD_ERR_READ_ERROR;
    }
    /* Use direct lookup */
    track->sector_size = SECTOR_SIZE_LOOKUP[track->sector_size_code];
    if (track->sector_size == 0) {
        DEBUG_PRINTF("DEBUG: imd_read_track_header: Invalid sector size derived from code %u. Returning IMD_ERR_READ_ERROR.\n", track->sector_size_code);
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_read_track_header: fseek back failed after invalid sector size.\n");
        }
        return IMD_ERR_READ_ERROR;
    }

    DEBUG_PRINTF("DEBUG: imd_read_track_header: Decoded: Mode=%u, Cyl=%u, Head=%u, HFlag=0x%02X, Nsec=%u, SSizeCode=%u, SectorSize=%u\n",
        track->mode, track->cyl, track->head, track->hflag, track->num_sectors, track->sector_size_code, track->sector_size);


    /* Read optional maps */
    if (track->num_sectors > 0) {
        DEBUG_PRINTF("DEBUG: imd_read_track_header: Reading %u smap bytes\n", track->num_sectors);
        if (read_bytes(track->smap, track->num_sectors, fimd) != track->num_sectors) {
            DEBUG_PRINTF("DEBUG: imd_read_track_header: Error/EOF reading Smap. Returning IMD_ERR_READ_ERROR.\n");
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                DEBUG_PRINTF("DEBUG: imd_read_track_header: fseek back failed after smap read error.\n");
            }
            return IMD_ERR_READ_ERROR;
        }
        if (track->hflag & IMD_HFLAG_CMAP_PRES) {
            DEBUG_PRINTF("DEBUG: imd_read_track_header: Reading %u cmap bytes\n", track->num_sectors);
            if (read_bytes(track->cmap, track->num_sectors, fimd) != track->num_sectors) {
                DEBUG_PRINTF("DEBUG: imd_read_track_header: Error/EOF reading Cmap. Returning IMD_ERR_READ_ERROR.\n");
                if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                    DEBUG_PRINTF("DEBUG: imd_read_track_header: fseek back failed after cmap read error.\n");
                }
                return IMD_ERR_READ_ERROR;
            }
        }
        if (track->hflag & IMD_HFLAG_HMAP_PRES) {
            DEBUG_PRINTF("DEBUG: imd_read_track_header: Reading %u hmap bytes\n", track->num_sectors);
            if (read_bytes(track->hmap, track->num_sectors, fimd) != track->num_sectors) {
                DEBUG_PRINTF("DEBUG: imd_read_track_header: Error/EOF reading Hmap. Returning IMD_ERR_READ_ERROR.\n");
                if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                    DEBUG_PRINTF("DEBUG: imd_read_track_header: fseek back failed after hmap read error.\n");
                }
                return IMD_ERR_READ_ERROR;
            }
        }
    }

    /* Skip over sector data records */
    DEBUG_PRINTF("DEBUG: imd_read_track_header: Skipping data for %u sectors\n", track->num_sectors);
    for (uint8_t i = 0; i < track->num_sectors; ++i) {
        uint8_t sector_type;
        DEBUG_PRINTF("DEBUG:   imd_read_track_header: Reading flag for sector %u\n", i);
        read_status = read_byte(fimd, &sector_type);
        if (read_status != 1) { /* Error or EOF reading flag */
            DEBUG_PRINTF("DEBUG: imd_read_track_header: Error/EOF reading flag in skip loop. Returning IMD_ERR_READ_ERROR.\n");
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                DEBUG_PRINTF("DEBUG: imd_read_track_header: fseek back failed after flag read error.\n");
            }
            return IMD_ERR_READ_ERROR;
        }
        /* Use the helper function to skip data based on the flag */
        /* Pass start_pos for error recovery within skip_sector_data */
        read_status = skip_sector_data(fimd, sector_type, track->sector_size, start_pos);
        if (read_status != 0) {
            DEBUG_PRINTF("DEBUG: imd_read_track_header: Error skipping data for sector %u. Returning %d.\n", i, read_status);
            /* skip_sector_data should have restored position */
            return read_status; /* Propagate specific error */
        }
    }

    /* Ensure data fields are NULL/0 as we didn't load data */
    track->data = NULL;
    track->data_size = 0;
    track->loaded = 0; /* Mark as header/maps only loaded */

    DEBUG_PRINTF("DEBUG: imd_read_track_header: Success for C%u H%u. Returning 1.\n", track->cyl, track->head);
    return 1; /* Success */
}

/* Reads header/maps/flags, skips data */
int imd_read_track_header_and_flags(FILE* fimd, ImdTrackInfo* track) {
    uint8_t head_byte;
    int read_status;
    long start_pos;
    uint8_t raw_mode, raw_cyl, raw_nsec, raw_ssize; /* For debug */

    if (!fimd || !track) {
        /* Check for NULL arguments */
        DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Invalid argument (fimd or track is NULL). Returning IMD_ERR_INVALID_ARG.\n");
        return IMD_ERR_INVALID_ARG;
    }

    start_pos = ftell(fimd);
    DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: ENTER. Pos=%ld, EOF=%d, Err=%d\n",
        start_pos, feof(fimd), ferror(fimd));

    /* start_pos already fetched */
    if (start_pos < 0) {
        DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: ftell failed. Returning IMD_ERR_SEEK_ERROR.\n");
        return IMD_ERR_SEEK_ERROR;
    }

    memset(track, 0, sizeof(ImdTrackInfo)); /* Initialize track structure */

    /* Read track header bytes */
    clearerr(fimd); /* Clear status before reading header */
    read_status = read_byte(fimd, &raw_mode);
    if (read_status <= 0) { /* EOF or error before first byte */
        DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: EOF or Error (%d) reading first byte. Pos=%ld.\n", read_status, start_pos);
        if (read_status < 0) {
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back only on error */
                DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: fseek back failed after read error.\n");
            }
            return IMD_ERR_READ_ERROR; /* Return specific error */
        }
        return 0; /* Return 0 for clean EOF */
    }
    /* If we read the first byte, subsequent non-success (EOF or error) is a failure */
    if (read_byte(fimd, &raw_cyl) != 1 ||
        read_byte(fimd, &head_byte) != 1 ||
        read_byte(fimd, &raw_nsec) != 1 ||
        read_byte(fimd, &raw_ssize) != 1)
    {
        DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Error/EOF reading track header fields. Returning IMD_ERR_READ_ERROR.\n");
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: fseek back failed after header read error.\n");
        }
        return IMD_ERR_READ_ERROR;
    }

    DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Raw Header Bytes: Mode=0x%02X, Cyl=0x%02X, HeadByte=0x%02X, Nsec=0x%02X, SSize=0x%02X\n",
        raw_mode, raw_cyl, head_byte, raw_nsec, raw_ssize);


    /* Assign and Decode header fields */
    track->mode = raw_mode;
    track->cyl = raw_cyl;
    track->num_sectors = raw_nsec; /* Assignment */
    track->sector_size_code = raw_ssize;
    track->head = head_byte & IMD_HFLAG_HEAD_MASK;
    track->hflag = head_byte & IMD_HFLAG_MASK;

    /* Validate decoded fields */
    if (track->mode >= LIBIMD_NUM_MODES || track->head > 1 || track->sector_size_code >= SECTOR_SIZE_LOOKUP_COUNT) {
        DEBUG_PRINTF("ERROR: imd_read_track_header_and_flags: Invalid decoded header field (mode=%u, head=%u, size_code=%u). Returning IMD_ERR_READ_ERROR.\n", track->mode, track->head, track->sector_size_code);
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: fseek back failed after invalid header.\n");
        }
        return IMD_ERR_READ_ERROR;
    }
    /* Use direct lookup */
    track->sector_size = SECTOR_SIZE_LOOKUP[track->sector_size_code];
    if (track->sector_size == 0) {
        DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Invalid sector size derived from code %u. Returning IMD_ERR_READ_ERROR.\n", track->sector_size_code);
        if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
            DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: fseek back failed after invalid sector size.\n");
        }
        return IMD_ERR_READ_ERROR;
    }

    DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Decoded: Mode=%u, Cyl=%u, Head=%u, HFlag=0x%02X, Nsec=%u, SSizeCode=%u, SectorSize=%u\n",
        track->mode, track->cyl, track->head, track->hflag, track->num_sectors, track->sector_size_code, track->sector_size);


    /* Read optional maps */
    if (track->num_sectors > 0) {
        DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Reading %u smap bytes\n", track->num_sectors);
        if (read_bytes(track->smap, track->num_sectors, fimd) != track->num_sectors) {
            DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Error/EOF reading Smap. Returning IMD_ERR_READ_ERROR.\n");
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: fseek back failed after smap read error.\n");
            }
            return IMD_ERR_READ_ERROR;
        }
        if (track->hflag & IMD_HFLAG_CMAP_PRES) {
            DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Reading %u cmap bytes\n", track->num_sectors);
            if (read_bytes(track->cmap, track->num_sectors, fimd) != track->num_sectors) {
                DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Error/EOF reading Cmap. Returning IMD_ERR_READ_ERROR.\n");
                if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                    DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: fseek back failed after cmap read error.\n");
                }
                return IMD_ERR_READ_ERROR;
            }
        }
        if (track->hflag & IMD_HFLAG_HMAP_PRES) {
            DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Reading %u hmap bytes\n", track->num_sectors);
            if (read_bytes(track->hmap, track->num_sectors, fimd) != track->num_sectors) {
                DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Error/EOF reading Hmap. Returning IMD_ERR_READ_ERROR.\n");
                if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                    DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: fseek back failed after hmap read error.\n");
                }
                return IMD_ERR_READ_ERROR;
            }
        }
    }


    /* Read flags and skip over sector data records based on flags read */
    DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Reading flags and skipping data for %u sectors\n", track->num_sectors);
    for (uint8_t i = 0; i < track->num_sectors; ++i) {
        uint8_t sector_type;
        /* Read flag individually */
        read_status = read_byte(fimd, &sector_type); /* Check return value */
        if (read_status != 1) { /* Error or EOF reading flag */
            DEBUG_PRINTF("ERROR: imd_read_track_header_and_flags: ERROR reading flag for sector %u (status=%d, errno=%d, feof=%d, ferror=%d). Returning IMD_ERR_READ_ERROR.\n",
                i, read_status, errno, feof(fimd), ferror(fimd));
            if (fseek(fimd, start_pos, SEEK_SET) != 0) { /* Seek back */
                DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: fseek back failed after flag read error.\n");
            }
            return IMD_ERR_READ_ERROR;
        }
        track->sflag[i] = sector_type; /* Store the flag */

        DEBUG_PRINTF("DEBUG:   imd_read_track_header_and_flags: Skipping data for sector index %u (flag 0x%02X)\n", i, sector_type);
        /* Pass start_pos for error recovery within skip_sector_data */
        read_status = skip_sector_data(fimd, sector_type, track->sector_size, start_pos);
        if (read_status != 0) {
            DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Error skipping data for sector %u. Returning %d.\n", i, read_status);
            /* skip_sector_data should have restored position */
            return read_status; /* Propagate specific error */
        }
    }

    /* Ensure data fields are NULL/0 as we didn't load data */
    track->data = NULL;
    track->data_size = 0;
    track->loaded = 0; /* Mark as header/maps/flags only loaded */

    DEBUG_PRINTF("DEBUG: imd_read_track_header_and_flags: Success for C%u H%u. Returning 1.\n", track->cyl, track->head);
    return 1; /* Success */
}

/* Checks if track has valid sectors */
/* Updated to use helper macros */
int imd_track_has_valid_sectors(FILE* fimd, uint8_t cyl, uint8_t head) {
    ImdTrackInfo track;
    long original_pos;
    int target_track_found = 0;      /* Flag: Have we encountered the target track? */
    int target_track_has_valid = 0; /* Result for the target track (0 or 1) */
    int final_result = IMD_ERR_TRACK_NOT_FOUND; /* Default return if target not found */
    int read_status;
    long current_pos; /* For debugging */

    if (!fimd) return IMD_ERR_INVALID_ARG;

    original_pos = ftell(fimd);
    if (original_pos < 0) {
        DEBUG_PRINTF("DEBUG: imd_track_has_valid_sectors: ftell failed at start. Returning IMD_ERR_SEEK_ERROR.\n");
        return IMD_ERR_SEEK_ERROR;
    }

    /* Rewind to start to ensure we scan all tracks */
    if (fseek(fimd, 0, SEEK_SET) != 0) {
        DEBUG_PRINTF("DEBUG: imd_track_has_valid_sectors: fseek to start failed. Returning IMD_ERR_SEEK_ERROR.\n");
        return IMD_ERR_SEEK_ERROR;
    }

    /* Skip header and comment */
    read_status = imd_read_file_header(fimd, NULL, NULL, 0);
    if (read_status != 0) {
        DEBUG_PRINTF("DEBUG: imd_track_has_valid_sectors: Error reading header (%d).\n", read_status);
        if (fseek(fimd, original_pos, SEEK_SET) != 0) {
            /* Log if restoring file position fails. */
            DEBUG_PRINTF("DEBUG: imd_track_has_valid_sectors: fseek to restore original_pos failed.\n");
            /* Despite fseek failure, proceed to return the original error. */
        }
        return read_status;
    }
    read_status = imd_skip_comment_block(fimd);
    if (read_status != 0) {
        DEBUG_PRINTF("DEBUG: imd_track_has_valid_sectors: Error skipping comment (%d).\n", read_status);
        if (fseek(fimd, original_pos, SEEK_SET) != 0) {
            /* Log if restoring file position fails. */
            DEBUG_PRINTF("DEBUG: imd_track_has_valid_sectors: fseek to restore original_pos failed.\n");
            /* Despite fseek failure, proceed to return the original error. */
        }
        return read_status;
    }

    /* Scan tracks */
    DEBUG_PRINTF("DEBUG: Scanning tracks for C%u H%u ---\n", (unsigned)cyl, (unsigned)head);
    while (1) {
        current_pos = ftell(fimd); /* Debug */
        if (current_pos < 0) { /* Debug check */
            DEBUG_PRINTF("ERROR: imd_track_has_valid_sectors: ftell failed before read_track_header_and_flags\n");
            /* Return the seek error. */
            final_result = IMD_ERR_SEEK_ERROR;
            break;
        }
        DEBUG_PRINTF("DEBUG:   Calling read_track_header_and_flags at pos %ld\n", current_pos);

        /* Use the function that reads flags and skips data */
        read_status = imd_read_track_header_and_flags(fimd, &track);
        DEBUG_PRINTF("DEBUG:   read_status = %d\n", read_status);

        if (read_status == 0) { /* Clean EOF */
            DEBUG_PRINTF("DEBUG:   EOF reached.\n");
            /* Loop finished. final_result is already set correctly */
            /* (either IMD_ERR_TRACK_NOT_FOUND or the result for the target track) */
            break;
        }
        if (read_status < 0) { /* Error during read/skip */
            DEBUG_PRINTF("DEBUG:   Read/skip error (%d).\n", read_status);
            /* We already found the target track, return its status. */
            final_result = read_status;
            break; /* Exit loop on error */
        }

        DEBUG_PRINTF("DEBUG:   Read track C%u H%u (NumSectors: %u)\n", (unsigned)track.cyl, (unsigned)track.head, (unsigned)track.num_sectors);

        /* Check if this is the target track */
        if (track.cyl == cyl && track.head == head) {
            DEBUG_PRINTF("DEBUG:   Found target track C%u H%u.\n", (unsigned)cyl, (unsigned)head);
            target_track_found = 1;
            target_track_has_valid = 0; /* Assume no valid sectors initially */
            for (uint8_t i = 0; i < track.num_sectors; ++i) {
                DEBUG_PRINTF("DEBUG:     Checking sector index %u, flag 0x%02X\n", i, track.sflag[i]);
                /* Check if the sector record type indicates data is present */
                /* A sector is "valid" if it's not IMD_SDR_UNAVAILABLE */
                if (track.sflag[i] != IMD_SDR_UNAVAILABLE) {
                    DEBUG_PRINTF("DEBUG:     Sector %u is VALID (non-unavailable).\n", i);
                    target_track_has_valid = 1; /* Found a valid sector */
                    break;      /* No need to check further for this track */
                }
            }
            DEBUG_PRINTF("DEBUG:   Final result for target track: %d\n", target_track_has_valid);
            final_result = target_track_has_valid; /* Store the result for the target */
            /* NOTE: Do not break here. Need to continue scanning in case the */
            /* file format allows duplicate tracks (though IMDU tools might not). */
            /* For this function's purpose (finding *if* a valid track exists), */
            /* finding the first match is sufficient. Let's break. */
            break; /* Found the target track, exit loop */
        }
        /* Not the target track, continue loop */
        current_pos = ftell(fimd); /* Check position after processing track */
        DEBUG_PRINTF("DEBUG:   Finished processing C%u H%u, file pos is now %ld\n", (unsigned)track.cyl, (unsigned)track.head, current_pos);
        if (current_pos < 0) { /* Check ftell result */
            DEBUG_PRINTF("ERROR: imd_track_has_valid_sectors: ftell failed after processing track\n");
            final_result = target_track_found ? final_result : IMD_ERR_SEEK_ERROR;
            break;
        }
    } /* end while */

    DEBUG_PRINTF("DEBUG: --- Finished scanning ---\n");
    if (fseek(fimd, original_pos, SEEK_SET) != 0) {
        /* Log if restoring file position fails. */
        DEBUG_PRINTF("DEBUG: imd_track_has_valid_sectors: fseek to restore original_pos failed.\n");
        /* Despite fseek failure, proceed to return the original error. */
    }

    return final_result; /* Return the determined result */
}


int imd_is_uniform(const uint8_t* data, size_t size, uint8_t* fill_byte_out) {
    if (size == 0) return 1; /* Empty is considered uniform */
    if (!data || !fill_byte_out) return 0; /* Invalid args */
    *fill_byte_out = data[0]; /* Store the first byte */
    /* Check if all other bytes match the first one */
    for (size_t i = 1; i < size; ++i) {
        if (data[i] != *fill_byte_out) {
            return 0; /* Found a non-matching byte */
        }
    }
    return 1; /* All bytes matched */
}

int imd_calculate_best_interleave(ImdTrackInfo* track) {
    /* Return 1 if NULL track or less than 2 sectors */
    if (!track || track->num_sectors < 2) return 1;

    uint8_t sorted_smap[LIBIMD_MAX_SECTORS_PER_TRACK];
    uint8_t sector_pos[LIBIMD_MAX_SECTORS_PER_TRACK]; /* Stores physical position of each logical sector ID */
    uint8_t interleave_counts[LIBIMD_MAX_SECTORS_PER_TRACK] = { 0 }; /* Counts occurrences of each interleave distance */
    int max_count = 0;
    int best_interleave = 1; /* Default to 1 */

    /* Initialize sector_pos array with an invalid marker */
    memset(sector_pos, LIBIMD_INVALID_SECTOR_POS, sizeof(sector_pos));

    /* Create mapping from logical sector ID to its physical position (index in smap) */
    for (int i = 0; i < track->num_sectors; ++i) {
        /* Warn if a sector ID is duplicated */
        if (sector_pos[track->smap[i]] != LIBIMD_INVALID_SECTOR_POS) {
            DEBUG_PRINTF("WARNING: imd_calculate_best_interleave: Duplicate sector ID %u found at physical index %d (previous was %d).\n",
                track->smap[i], i, sector_pos[track->smap[i]]);
            /* Ignore this duplicate for calculation? Or return error? */
            /* Current behavior: Overwrites previous position, might skew result. */
        }
        sector_pos[track->smap[i]] = (uint8_t)i; /* Store physical position */
    }

    /* Create a sorted copy of the sector map */
    memcpy(sorted_smap, track->smap, track->num_sectors);
    /* Simple bubble sort for small N */
    for (int i = 0; i < track->num_sectors - 1; ++i) {
        for (int j = 0; j < track->num_sectors - i - 1; ++j) {
            if (sorted_smap[j] > sorted_smap[j + 1]) {
                uint8_t temp = sorted_smap[j];
                sorted_smap[j] = sorted_smap[j + 1];
                sorted_smap[j + 1] = temp;
            }
        }
    }

    /* Calculate distances between logically sequential sectors */
    for (int i = 0; i < track->num_sectors; ++i) {
        uint8_t current_sec_id = sorted_smap[i];
        uint8_t next_sec_id = sorted_smap[(i + 1) % track->num_sectors]; /* Wrap around for last->first */
        uint8_t current_pos = LIBIMD_INVALID_SECTOR_POS;
        uint8_t next_pos = LIBIMD_INVALID_SECTOR_POS;

        /* Get physical positions using the map created earlier */
        current_pos = sector_pos[current_sec_id];
        next_pos = sector_pos[next_sec_id];

        /* Skip if either sector ID wasn't found or was out of bounds */
        if (current_pos == LIBIMD_INVALID_SECTOR_POS || next_pos == LIBIMD_INVALID_SECTOR_POS) continue;

        /* Calculate physical distance, handling wrap-around */
        int distance = (next_pos >= current_pos) ? (next_pos - current_pos) : (track->num_sectors - (current_pos - next_pos));

        /* Increment count for this distance if valid */
        if (distance > 0 && distance < track->num_sectors) {
            interleave_counts[distance]++;
        }
    }

    /* Find the distance with the highest count */
    for (int i = 1; i < track->num_sectors; ++i) {
        if (interleave_counts[i] > max_count) {
            max_count = interleave_counts[i];
            best_interleave = i;
        }
    }
    DEBUG_PRINTF("DEBUG: imd_calculate_best_interleave: Calculated best interleave as %d (count=%d) for C%u H%u.\n", best_interleave, max_count, track->cyl, track->head);
    return best_interleave;
}

int imd_apply_interleave(ImdTrackInfo* track, int interleave_factor) {
    if (!track || !track->loaded || !track->data || track->num_sectors < 2 || interleave_factor < 1) {
        DEBUG_PRINTF("DEBUG: imd_apply_interleave: Invalid argument or track state (loaded=%d, data=%p, nsec=%d, il=%d).\n",
            track ? track->loaded : -1, track ? track->data : NULL, track ? track->num_sectors : 0, interleave_factor);
        return IMD_ERR_INVALID_ARG; /* Invalid args or state */
    }

    uint8_t n = track->num_sectors;
    uint8_t original_smap[LIBIMD_MAX_SECTORS_PER_TRACK];
    uint8_t original_cmap[LIBIMD_MAX_SECTORS_PER_TRACK];
    uint8_t original_hmap[LIBIMD_MAX_SECTORS_PER_TRACK];
    uint8_t original_sflag[LIBIMD_MAX_SECTORS_PER_TRACK];
    uint8_t* original_data = NULL;
    uint8_t logical_to_physical[LIBIMD_MAX_SECTORS_PER_TRACK]; /* Maps sorted logical ID index to original physical index */
    uint8_t physical_pos_used[LIBIMD_MAX_SECTORS_PER_TRACK] = { 0 }; /* Tracks which physical slots have been filled */
    int current_physical_pos = 0; /* Target physical position for the current sector */

    DEBUG_PRINTF("DEBUG: imd_apply_interleave: Applying interleave %d to C%u H%u (%u sectors).\n", interleave_factor, track->cyl, track->head, n);

    /* 1. Backup original track state (maps and data) */
    memcpy(original_smap, track->smap, n);
    memcpy(original_cmap, track->cmap, n);
    memcpy(original_hmap, track->hmap, n);
    memcpy(original_sflag, track->sflag, n);
    original_data = (uint8_t*)malloc(track->data_size);
    if (!original_data) {
        DEBUG_PRINTF("ERROR: imd_apply_interleave: Failed to allocate buffer for original data backup.\n");
        return IMD_ERR_ALLOC; /* Allocation failed */
    }
    memcpy(original_data, track->data, track->data_size);

    /* 2. Create sorted list of logical sector IDs */
    uint8_t sorted_smap[LIBIMD_MAX_SECTORS_PER_TRACK];
    memcpy(sorted_smap, original_smap, n);
    /* Simple bubble sort for small N */
    for (int i = 0; i < n - 1; ++i) {
        for (int j = 0; j < n - i - 1; ++j) {
            if (sorted_smap[j] > sorted_smap[j + 1]) {
                uint8_t temp = sorted_smap[j]; sorted_smap[j] = sorted_smap[j + 1]; sorted_smap[j + 1] = temp;
            }
        }
    }
    /* Find original physical index for each sorted logical sector ID */
    /* logical_to_physical[i] = original physical index of the i-th logically sorted sector */
    for (int i = 0; i < n; ++i) {
        logical_to_physical[i] = LIBIMD_INVALID_SECTOR_POS; /* Use sentinel */
        for (int k = 0; k < n; ++k) {
            if (original_smap[k] == sorted_smap[i]) {
                logical_to_physical[i] = (uint8_t)k; /* Found original index */
                break;
            }
        }
        if (logical_to_physical[i] == LIBIMD_INVALID_SECTOR_POS) {
            /* Error: Sector ID from sorted list not found in original map (should not happen if input is valid) */
            DEBUG_PRINTF("ERROR: imd_apply_interleave: Logical sector ID %u not found in original smap.\n", sorted_smap[i]);
            free(original_data);
            return IMD_ERR_SECTOR_NOT_FOUND; /* Logical error, map invalid */
        }
    }

    /* 3. Place sectors into new physical positions based on interleave */
    for (int i = 0; i < n; ++i) { /* Iterate through sectors in logical order (index 'i') */
        /* Find the next available physical slot */
        while (physical_pos_used[current_physical_pos]) {
            current_physical_pos = (current_physical_pos + 1) % n;
        }

        /* Get the original physical index of the current logical sector (sorted_smap[i]) */
        uint8_t original_index = logical_to_physical[i];

        /* Place the data and maps from the 'original_index' into the 'current_physical_pos' */
        track->smap[current_physical_pos] = original_smap[original_index];
        track->cmap[current_physical_pos] = original_cmap[original_index];
        track->hmap[current_physical_pos] = original_hmap[original_index];
        track->sflag[current_physical_pos] = original_sflag[original_index];
        if (track->sector_size > 0) { /* Avoid memcpy with size 0 */
            memcpy(track->data + ((size_t)current_physical_pos * track->sector_size),
                original_data + ((size_t)original_index * track->sector_size),
                track->sector_size);
        }
        physical_pos_used[current_physical_pos] = 1; /* Mark this physical slot as filled */

        /* Advance the target physical position pointer by the interleave factor for the *next* logical sector */
        current_physical_pos = (current_physical_pos + interleave_factor) % n;
    }

    free(original_data); /* Free the backup buffer */
    return 0; /* Success */
}

/* Updated imd_write_track_imd to use compression_mode */
int imd_write_track_imd(FILE* fout, ImdTrackInfo* track, const ImdWriteOpts* opts) {
    if (!fout || !track || !opts) return IMD_ERR_INVALID_ARG; /* Check args */
    if (!track->loaded) {
        DEBUG_PRINTF("DEBUG: imd_write_track_imd: Error - Track data not loaded for C%u H%u.\n", track->cyl, track->head);
        return IMD_ERR_INVALID_ARG; /* Cannot write unloaded track */
    }

    /* --- Prepare data and flags for writing --- */
    uint8_t final_sflag[LIBIMD_MAX_SECTORS_PER_TRACK]; /* Flags to actually write */
    uint8_t final_mode = track->mode;
    ImdTrackInfo track_to_write = *track; /* Work on a copy to allow modification (interleave) */
    uint8_t* interleaved_data = NULL; /* Buffer for interleaved data if needed */
    int ret_status = 0; /* Assume success initially */

    /* Create a copy of data if interleaving is requested and there's data */
    if (opts->interleave_factor != LIBIMD_IL_AS_READ && track->num_sectors > 1 && track->data_size > 0) {
        DEBUG_PRINTF("DEBUG: imd_write_track_imd: Allocating buffer for interleave copy (%zu bytes).\n", track->data_size);
        interleaved_data = (uint8_t*)malloc(track->data_size);
        if (!interleaved_data) return IMD_ERR_ALLOC;
        memcpy(interleaved_data, track->data, track->data_size);
        track_to_write.data = interleaved_data; /* Point copy to copied data */
    }
    else {
        /* No interleaving or no data, point copy to original data */
        track_to_write.data = track->data;
    }

    /* Apply Interleave (modifies track_to_write maps and data in place) */
    int il_factor = opts->interleave_factor;
    if (il_factor != LIBIMD_IL_AS_READ && track_to_write.num_sectors > 1) {
        if (il_factor == LIBIMD_IL_BEST_GUESS) {
            /* Calculate best guess based on the possibly modified track_to_write (maps are still original here) */
            il_factor = imd_calculate_best_interleave(&track_to_write);
            DEBUG_PRINTF("DEBUG: imd_write_track_imd: Best guess interleave calculated as %d.\n", il_factor);
        }
        /* Apply the determined interleave factor */
        ret_status = imd_apply_interleave(&track_to_write, il_factor);
        if (ret_status != 0) {
            DEBUG_PRINTF("ERROR: imd_write_track_imd: Failed to apply interleave %d (status %d).\n", il_factor, ret_status);
            if (interleaved_data) free(interleaved_data);
            return ret_status; /* Failed to apply interleave, return specific error */
        }
        /* track_to_write now contains interleaved maps and data */
    }

    /* Apply Mode Translation */
    if (track_to_write.mode < LIBIMD_NUM_MODES) {
        final_mode = opts->tmode[track_to_write.mode];
        if (final_mode != track_to_write.mode) {
            DEBUG_PRINTF("DEBUG: imd_write_track_imd: Translating mode %u to %u for C%u H%u.\n", track_to_write.mode, final_mode, track_to_write.cyl, track_to_write.head);
        }
    }
    else {
        DEBUG_PRINTF("WARNING: imd_write_track_imd: Original mode %u is invalid, writing as is.\n", track_to_write.mode);
        final_mode = track_to_write.mode; /* Write invalid mode as is */
    }


    /* Sector Flag/Type Processing (Determine flags to write based on data and options) */
    for (uint8_t i = 0; i < track_to_write.num_sectors; ++i) {
        /* Get the original flag from the (potentially interleaved) track copy */
        uint8_t original_flag = track_to_write.sflag[i];
        uint8_t target_base_type; /* IMD_SDR_NORMAL or IMD_SDR_COMPRESSED */
        int target_has_dam = 0;
        int target_has_err = 0;

        /* Handle Unavailable sectors first */
        if (original_flag == IMD_SDR_UNAVAILABLE) {
            /* If original sector is unavailable, keep it that way */
            final_sflag[i] = IMD_SDR_UNAVAILABLE;
            DEBUG_PRINTF("DEBUG:   imd_write_track_imd: Sector %u: Original=UNAVAILABLE -> Final=UNAVAILABLE\n", i);
            continue; /* Move to next sector */
        }

        /* Sector has data (or was flagged as having data), check uniformity */
        uint8_t* sector_data = NULL;
        if (track_to_write.data && track_to_write.data_size >= ((size_t)(i + 1) * track_to_write.sector_size)) {
            /* Ensure offset calculation is safe */
            sector_data = track_to_write.data + ((size_t)i * track_to_write.sector_size);
        }
        else if (track_to_write.sector_size > 0) {
            /* This case indicates an inconsistency: flag suggests data, but buffer is too small or null */
            DEBUG_PRINTF("ERROR:   imd_write_track_imd: Data buffer inconsistent for sector %u (flag=0x%02X, size=%zu, offset=%zu, data_ptr=%p)\n",
                i, original_flag, track_to_write.data_size, (size_t)i * track_to_write.sector_size, track_to_write.data);
            ret_status = IMD_ERR_INVALID_ARG; /* Or a more specific internal error? */
            if (interleaved_data) free(interleaved_data);
            return ret_status;
        }

        int is_uniform_sector = 0;
        uint8_t fill_byte = 0; /* Used later if compressing */
        if (sector_data && track_to_write.sector_size > 0) {
            is_uniform_sector = imd_is_uniform(sector_data, track_to_write.sector_size, &fill_byte);
        }

        /* Determine target base type based on uniformity and compression_mode option */
        switch (opts->compression_mode) {
        case IMD_COMPRESSION_FORCE_COMPRESS:
            /* Force compression if uniform, otherwise normal */
            target_base_type = (is_uniform_sector) ? IMD_SDR_COMPRESSED : IMD_SDR_NORMAL;
            break;
        case IMD_COMPRESSION_FORCE_DECOMPRESS:
            /* Force normal regardless of uniformity */
            target_base_type = IMD_SDR_NORMAL;
            break;
        case IMD_COMPRESSION_AS_READ:
        default: /* Treat default/unknown as AS_READ */
            /* If original was compressed, write compressed only if still uniform */
            if (IMD_SDR_IS_COMPRESSED(original_flag)) {
                target_base_type = (is_uniform_sector) ? IMD_SDR_COMPRESSED : IMD_SDR_NORMAL;
            }
            /* If original was normal, write normal */
            else {
                target_base_type = IMD_SDR_NORMAL;
            }
            break;
        }

        /* Determine final status bits (DAM, ERR), applying forcing options */
        /* Keep DAM flag unless force_non_deleted is set */
        target_has_dam = IMD_SDR_HAS_DAM(original_flag) && !opts->force_non_deleted;
        /* Keep ERR flag unless force_non_bad is set */
        target_has_err = IMD_SDR_HAS_ERR(original_flag) && !opts->force_non_bad;

        /* Combine base type and status bits to get the final sector flag */
        if (target_base_type == IMD_SDR_NORMAL) {
            if (target_has_dam && target_has_err) final_sflag[i] = IMD_SDR_DELETED_ERR;
            else if (target_has_err) final_sflag[i] = IMD_SDR_NORMAL_ERR;
            else if (target_has_dam) final_sflag[i] = IMD_SDR_NORMAL_DAM;
            else final_sflag[i] = IMD_SDR_NORMAL;
        }
        else { /* target_base_type == IMD_SDR_COMPRESSED */
            if (target_has_dam && target_has_err) final_sflag[i] = IMD_SDR_COMPRESSED_DEL_ERR;
            else if (target_has_err) final_sflag[i] = IMD_SDR_COMPRESSED_ERR;
            else if (target_has_dam) final_sflag[i] = IMD_SDR_COMPRESSED_DAM;
            else final_sflag[i] = IMD_SDR_COMPRESSED;
        }
        DEBUG_PRINTF("DEBUG:   imd_write_track_imd: Sector %u: Orig=0x%02X, Opts(C:%d,NB:%d,ND:%d), Uniform=%d -> Final=0x%02X\n",
            i, original_flag, opts->compression_mode, opts->force_non_bad, opts->force_non_deleted, is_uniform_sector, final_sflag[i]);
    }


    /* --- Write IMD Output --- */
    DEBUG_PRINTF("DEBUG: imd_write_track_imd: Writing track C%u H%u header.\n", track_to_write.cyl, track_to_write.head);
    if (fputc(final_mode, fout) == EOF ||
        fputc(track_to_write.cyl, fout) == EOF ||
        fputc(track_to_write.head | track_to_write.hflag, fout) == EOF || /* Combine head number and flags */
        fputc(track_to_write.num_sectors, fout) == EOF ||
        fputc(track_to_write.sector_size_code, fout) == EOF) {
        DEBUG_PRINTF("ERROR: imd_write_track_imd: Error writing track header bytes.\n");
        if (interleaved_data) free(interleaved_data);
        return IMD_ERR_WRITE_ERROR;
    }

    /* Write Maps (only if sectors exist) */
    if (track_to_write.num_sectors > 0) {
        DEBUG_PRINTF("DEBUG:   imd_write_track_imd: Writing smap (%u bytes).\n", track_to_write.num_sectors);
        if (write_bytes(track_to_write.smap, track_to_write.num_sectors, fout) != 0) { goto write_error; }
        if (track_to_write.hflag & IMD_HFLAG_CMAP_PRES) {
            DEBUG_PRINTF("DEBUG:   imd_write_track_imd: Writing cmap (%u bytes).\n", track_to_write.num_sectors);
            if (write_bytes(track_to_write.cmap, track_to_write.num_sectors, fout) != 0) { goto write_error; }
        }
        if (track_to_write.hflag & IMD_HFLAG_HMAP_PRES) {
            DEBUG_PRINTF("DEBUG:   imd_write_track_imd: Writing hmap (%u bytes).\n", track_to_write.num_sectors);
            if (write_bytes(track_to_write.hmap, track_to_write.num_sectors, fout) != 0) { goto write_error; }
        }
    }

    /* Write Sector Data Records */
    for (uint8_t i = 0; i < track_to_write.num_sectors; ++i) {
        uint8_t write_flag = final_sflag[i];
        DEBUG_PRINTF("DEBUG:   imd_write_track_imd: Writing flag 0x%02X for sector index %u\n", write_flag, i);
        if (fputc(write_flag, fout) == EOF) { goto write_error; }

        uint8_t* sector_data = NULL;
        /* Calculate pointer only if data exists and is large enough */
        if (track_to_write.data && track_to_write.data_size >= ((size_t)(i + 1) * track_to_write.sector_size)) {
            sector_data = track_to_write.data + ((size_t)i * track_to_write.sector_size);
        }

        /* Write data based on the final flag */
        if (IMD_SDR_HAS_DATA(write_flag)) {
            if (IMD_SDR_IS_COMPRESSED(write_flag)) { /* Compressed */
                uint8_t current_fill_byte = 0;
                /* We must have data if the flag indicates compression is possible */
                if (!sector_data) {
                    DEBUG_PRINTF("ERROR:   imd_write_track_imd: NULL data pointer for compressed sector %u\n", i);
                    ret_status = IMD_ERR_INVALID_ARG; /* Internal inconsistency */
                    goto write_error;
                }
                /* Re-check uniformity just before writing to get the fill byte */
                /* This assumes imd_is_uniform is efficient enough */
                (void)imd_is_uniform(sector_data, track_to_write.sector_size, &current_fill_byte);
                DEBUG_PRINTF("DEBUG:     imd_write_track_imd: Writing compressed fill byte 0x%02X\n", current_fill_byte);
                if (fputc(current_fill_byte, fout) == EOF) { goto write_error; }
            }
            else { /* Normal */
                if (track_to_write.sector_size > 0) {
                    /* Check we have data if size > 0 */
                    if (!sector_data) {
                        DEBUG_PRINTF("ERROR:   imd_write_track_imd: NULL data pointer for normal sector %u\n", i);
                        ret_status = IMD_ERR_INVALID_ARG; /* Internal inconsistency */
                        goto write_error;
                    }
                    DEBUG_PRINTF("DEBUG:     imd_write_track_imd: Writing %u bytes of normal data\n", track_to_write.sector_size);
                    if (write_bytes(sector_data, track_to_write.sector_size, fout) != 0) { goto write_error; }
                }
                else {
                    /* Normal flag but zero sector size - write nothing */
                    DEBUG_PRINTF("DEBUG:     imd_write_track_imd: Normal flag with zero sector size, writing no data.\n");
                }
            }
        }
        else {
            /* No data follows (Unavailable or invalid flag handled earlier) */
            DEBUG_PRINTF("DEBUG:     imd_write_track_imd: Flag 0x%02X indicates no data follows.\n", write_flag);
        }
    }


    if (interleaved_data) free(interleaved_data); /* Clean up copied data */
    return 0; /* Success */

write_error:
    DEBUG_PRINTF("ERROR: imd_write_track_imd: Write error occurred for C%u H%u.\n", track_to_write.cyl, track_to_write.head);
    if (interleaved_data) free(interleaved_data);
    return (ret_status != 0) ? ret_status : IMD_ERR_WRITE_ERROR; /* Return internal error code if set, else write error */
}

int imd_write_track_bin(FILE* fout, ImdTrackInfo* track, const ImdWriteOpts* opts) {
    if (!fout || !track || !opts) return IMD_ERR_INVALID_ARG; /* Check args */
    if (!track->loaded) {
        DEBUG_PRINTF("DEBUG: imd_write_track_bin: Error - Track data not loaded for C%u H%u.\n", track->cyl, track->head);
        return IMD_ERR_INVALID_ARG; /* Cannot write unloaded track */
    }

    ImdTrackInfo track_copy = *track; /* Work on a copy */
    uint8_t* data_copy = NULL; /* Buffer for interleaved data */
    int ret_status = 0; /* Assume success initially */

    /* Create a copy of data if interleaving is requested and data exists */
    if (opts->interleave_factor != LIBIMD_IL_AS_READ && track->num_sectors > 1 && track->data_size > 0) {
        DEBUG_PRINTF("DEBUG: imd_write_track_bin: Allocating buffer for interleave copy (%zu bytes).\n", track->data_size);
        data_copy = (uint8_t*)malloc(track->data_size);
        if (!data_copy) return IMD_ERR_ALLOC;
        memcpy(data_copy, track->data, track->data_size);
        track_copy.data = data_copy; /* Point copy to copied data */
    }
    else {
        /* No interleaving or no data, point copy to original data */
        track_copy.data = track->data;
    }

    /* Apply interleave if requested (modifies track_copy maps and data in place) */
    int il_factor = opts->interleave_factor;
    if (il_factor != LIBIMD_IL_AS_READ && track_copy.num_sectors > 1) {
        if (il_factor == LIBIMD_IL_BEST_GUESS) {
            /* Calculate best guess based on original track map */
            il_factor = imd_calculate_best_interleave(track); /* Use original track for guess */
            DEBUG_PRINTF("DEBUG: imd_write_track_bin: Best guess interleave calculated as %d.\n", il_factor);
        }
        /* Apply the determined interleave factor to the copy */
        ret_status = imd_apply_interleave(&track_copy, il_factor);
        if (ret_status != 0) {
            DEBUG_PRINTF("ERROR: imd_write_track_bin: Failed to apply interleave %d (status %d).\n", il_factor, ret_status);
            if (data_copy) free(data_copy);
            return ret_status; /* Failed to apply interleave, return specific error */
        }
    }

    /* Write raw data (potentially reordered from track_copy.data) */
    if (track_copy.num_sectors > 0 && track_copy.data && track_copy.data_size > 0) {
        /* Condition: We have sectors, a valid data pointer, and positive data size */
        DEBUG_PRINTF("DEBUG: imd_write_track_bin: Writing %zu bytes of track data for C%u H%u.\n", track_copy.data_size, track_copy.cyl, track_copy.head);
        if (write_bytes(track_copy.data, track_copy.data_size, fout) != 0) {
            DEBUG_PRINTF("ERROR: imd_write_track_bin: Error writing track data.\n");
            if (data_copy) free(data_copy);
            return IMD_ERR_WRITE_ERROR; /* Error writing data */
        }
    }
    /* ----- FIX START ----- */
    else if (track_copy.num_sectors > 0 && (!track_copy.data || track_copy.data_size == 0)) {
        /* Condition: We have sectors, but data pointer is NULL or data_size is 0 */
        DEBUG_PRINTF("ERROR: imd_write_track_bin: Track C%u H%u has %u sectors but data pointer is %p or data_size is %zu. Returning IMD_ERR_INVALID_ARG.\n",
            track_copy.cyl, track_copy.head, track_copy.num_sectors, track_copy.data, track_copy.data_size);
        if (data_copy) free(data_copy); /* Clean up if allocated */
        return IMD_ERR_INVALID_ARG; /* Return error as expected by test */
    }
    /* ----- FIX END ----- */
    else {
        /* Condition: Zero sectors - nothing to write */
        DEBUG_PRINTF("DEBUG: imd_write_track_bin: Track C%u H%u has 0 sectors. Writing nothing.\n", track_copy.cyl, track_copy.head);
        /* This is considered success */
    }

    if (data_copy) free(data_copy); /* Clean up copied data */
    return 0; /* Success */
}

/* Public function to expose the static write_bytes */
int imd_write_bytes(const void* buffer, size_t size, FILE* file) {
    /* Use internal helper which returns IMD_ERR_WRITE_ERROR */
    return write_bytes(buffer, size, file);
}

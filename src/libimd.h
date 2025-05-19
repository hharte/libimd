/*
 * ImageDisk Library (Cross-Platform) Definitions.
 *
 * www.github.com/hharte/libimd
 *
 * Copyright (c) 2025, Howard M. Harte
 *
 * The original MS-DOS version is available from Dave's Old Computers:
 * http://dunfield.classiccmp.org/img/
 *
 */

#ifndef LIBIMD_H
#define LIBIMD_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "libimd_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Constants --- */

#define LIBIMD_MAX_SECTORS_PER_TRACK 256
#define LIBIMD_MAX_SECTOR_SIZE 8192
#define LIBIMD_FILL_BYTE_DEFAULT 0xE5
#define LIBIMD_MAX_HEADER_LINE 256
#define LIBIMD_COMMENT_EOF_MARKER 0x1A
#define LIBIMD_NUM_MODES 6

/* IMD Mode Definitions (Index for mode field) */
#define IMD_MODE_FM_500     0   /* 500 kbps FM (Single Density) */
#define IMD_MODE_FM_300     1   /* 300 kbps FM (Single Density) */
#define IMD_MODE_FM_250     2   /* 250 kbps FM (Single Density) */
#define IMD_MODE_MFM_500    3   /* 500 kbps MFM (Double Density) */
#define IMD_MODE_MFM_300    4   /* 300 kbps MFM (Double Density) */
#define IMD_MODE_MFM_250    5   /* 250 kbps MFM (Double Density) */

/* Interleave constants for ImdWriteOpts */
#define LIBIMD_IL_AS_READ    0   /* Write sectors in the order they appear in the track structure */
#define LIBIMD_IL_BEST_GUESS 255 /* Calculate the most likely interleave and apply it before writing */

/* Head Flags (High nibble of the Head Byte in the IMD file) */
#define IMD_HFLAG_HEAD_MASK 0x0F /* Mask for physical head number (0 or 1) - Low nibble used */
#define IMD_HFLAG_CMAP_PRES 0x80 /* Bit 7: Cylinder Map Present */
#define IMD_HFLAG_HMAP_PRES 0x40 /* Bit 6: Head Map Present */
#define IMD_HFLAG_MASK      0xF0 /* Mask for all head flags */

/* Sector Data Record Types (Value of the Sector Data Record byte) */
#define IMD_SDR_UNAVAILABLE   0x00 /* Sector data unavailable - could not be read */
#define IMD_SDR_NORMAL        0x01 /* Normal data: (Sector Size) bytes follow */
#define IMD_SDR_COMPRESSED    0x02 /* Compressed: All bytes in sector have same value (xx) */
#define IMD_SDR_NORMAL_DAM    0x03 /* Normal data with "Deleted-Data address mark" */
#define IMD_SDR_COMPRESSED_DAM 0x04 /* Compressed with "Deleted-Data address mark" */
#define IMD_SDR_NORMAL_ERR    0x05 /* Normal data read with data error */
#define IMD_SDR_COMPRESSED_ERR 0x06 /* Compressed read with data error */
#define IMD_SDR_DELETED_ERR   0x07 /* Deleted data read with data error */
#define IMD_SDR_COMPRESSED_DEL_ERR 0x08 /* Compressed, Deleted read with data error */

/* Helper macros to check properties of a Sector Data Record type */
/* Checks if the record type indicates sector data is present (either normal or compressed) */
#define IMD_SDR_HAS_DATA(type) ((type) >= 0x01 && (type) <= 0x08)
/* Checks if the record type indicates compressed data */
#define IMD_SDR_IS_COMPRESSED(type) (((type) & 0x01) == 0 && (type) != 0x00) /* 0x02, 0x04, 0x06, 0x08 */
/* Checks if the record type indicates a Deleted Data Address Mark */
#define IMD_SDR_HAS_DAM(type) ((type - 1) & 0x02)
/* Checks if the record type indicates a data error occurred during read */
#define IMD_SDR_HAS_ERR(type) ((type - 1) & 0x04)

/* Side Mask Defines (Used for track exclusion etc. - Application level) */
#define IMD_SIDE_0_MASK     1  /* Bitmask for side 0 */
#define IMD_SIDE_1_MASK     2  /* Bitmask for side 1 */
#define IMD_SIDE_BOTH_MASK  3  /* Bitmask for both sides (SIDE_0 | SIDE_1) */

/* Compression Mode Defines for ImdWriteOpts */
#define IMD_COMPRESSION_AS_READ           0 /* Match original sector's compression state (if possible) */
#define IMD_COMPRESSION_FORCE_COMPRESS    1 /* Force compression for uniform sectors */
#define IMD_COMPRESSION_FORCE_DECOMPRESS  2 /* Force decompression (write as normal data) */

/* Error codes returned by library functions */
#define IMD_ERR_SECTOR_NOT_FOUND -10 /* Requested sector (logical ID or physical index) not found */
#define IMD_ERR_TRACK_NOT_FOUND  -11 /* Requested track (cyl/head) not found in the file */
#define IMD_ERR_READ_ERROR       -12 /* General file read error or unexpected EOF */
#define IMD_ERR_WRITE_ERROR      -13 /* General file write error */
#define IMD_ERR_SEEK_ERROR       -14 /* File seek operation failed */
#define IMD_ERR_INVALID_ARG      -15 /* Invalid argument passed to function (e.g., NULL pointer) */
#define IMD_ERR_BUFFER_TOO_SMALL -16 /* Provided buffer is smaller than the required sector size */
#define IMD_ERR_SIZE_MISMATCH    -17 /* Data size provided does not match sector size for write */
#define IMD_ERR_UNAVAILABLE      -18 /* Sector is marked as unavailable (Type 0x00) */
#define IMD_ERR_ALLOC            -19 /* Memory allocation failed */

/* --- Enums --- */

/* Specifies how a sector is identified in read/write calls */
typedef enum {
    IMD_SEC_LOGICAL_ID,  /* Identify sector by its logical ID (value in SMAP) */
    IMD_SEC_PHYSICAL_IDX /* Identify sector by its physical index (0 to num_sectors-1) */
} ImdSectorIdentifierType;


/* --- Data Structures --- */

/* Represents a single track's information and data loaded from an IMD file */
typedef struct {
    /* Header Information (Read from IMD file) */
    uint8_t  mode;             /* Data rate/density (0-5) */
    uint8_t  cyl;              /* Physical cylinder number (0-255) */
    uint8_t  head;             /* Physical head number (0-1) */
    uint8_t  hflag;            /* Head flags (IMD_HFLAG_CMAP_PRES, IMD_HFLAG_HMAP_PRES) */
    uint8_t  num_sectors;      /* Number of sectors in this track (0-255) */
    uint8_t  sector_size_code; /* Sector size code (0-6) */
    uint32_t sector_size;      /* Actual sector size in bytes (derived from code) */

    /* Maps (Read from IMD file) */
    uint8_t  smap[LIBIMD_MAX_SECTORS_PER_TRACK]; /* Sector numbering map (logical IDs) */
    uint8_t  cmap[LIBIMD_MAX_SECTORS_PER_TRACK]; /* Cylinder numbering map (logical IDs, optional) */
    uint8_t  hmap[LIBIMD_MAX_SECTORS_PER_TRACK]; /* Head numbering map (logical IDs, optional) */

    /* Sector Status/Data (Read from IMD file) */
    uint8_t  sflag[LIBIMD_MAX_SECTORS_PER_TRACK];/* Original IMD Sector Data Record byte read (0x00-0x08) */
    uint8_t* data;             /* Pointer to buffer holding all sector data (expanded) */
    size_t   data_size;        /* Total size of the allocated data buffer */
    int      loaded;           /* Flag: 1 if track data is loaded, 0 otherwise */
} ImdTrackInfo;

/* Structure to pass processing options to write functions */
typedef struct {
    /* Specifies how to handle sector compression on output.
     * Use IMD_COMPRESSION_* defines (0=As Read, 1=Compress, 2=Decompress).
     */
    int compression_mode;
    int force_non_bad;      /* If non-zero, try to write sectors with error flags as normal/compressed */
    int force_non_deleted;  /* If non-zero, try to write sectors with DAM flags as normal/compressed */
    uint8_t tmode[LIBIMD_NUM_MODES]; /* Mode translation map: tmode[read_mode] = write_mode */
    int interleave_factor;  /* Interleave to apply before writing (LIBIMD_IL_AS_READ, LIBIMD_IL_BEST_GUESS, 1-n) */
} ImdWriteOpts;

/* Structure to hold parsed IMD file header info */
typedef struct {
    char version[32];       /* Version string from header */
    int day, month, year;   /* Date components */
    int hour, minute, second; /* Time components */
} ImdHeaderInfo;


/* --- Public Function Prototypes --- */

/* --- Header and Comment Handling --- */

/**
 * Reads and parses the IMD text header line ("IMD version: date time").
 * @param fimd Input file stream, positioned at the start of the file.
 * @param header_info Optional pointer to structure to store parsed info. Can be NULL.
 * @param header_line_buf Optional buffer to store the raw header line. Can be NULL.
 * @param buf_size Size of header_line_buf. Ignored if header_line_buf is NULL.
 * @return 0 on success (header found and parsed/stored), negative IMD_ERR_* on error.
 */
int imd_read_file_header(FILE* fimd, ImdHeaderInfo* header_info, char* header_line_buf, size_t buf_size);

/**
 * Reads the comment block from the current file position until the EOF marker (0x1A).
 * Allocates memory for the comment. Caller must free the returned buffer.
 * @param fimd Input file stream, positioned immediately after the header line's CRLF.
 * @param comment_size_out Pointer to store the size of the read comment (excluding null terminator). Must not be NULL.
 * @return Pointer to the allocated comment buffer (null-terminated), or NULL on error (read error, EOF before marker, memory allocation failure) or if comment_size_out is NULL. Returns an empty, allocated string "" if the comment is empty.
 */
char* imd_read_comment_block(FILE* fimd, size_t* comment_size_out);

/**
 * Skips the comment block from the current file position until the EOF marker (0x1A).
 * @param fimd Input file stream, positioned immediately after the header line's CRLF.
 * @return 0 on success (marker found), negative IMD_ERR_* on error (EOF before marker or read error).
 */
int imd_skip_comment_block(FILE* fimd);

/**
 * Writes the standard IMD text header line with the current date/time.
 * @param fout Output file stream.
 * @param version_string Version string of the creating program (e.g., "1.19"). Must not be NULL.
 * @return 0 on success, negative IMD_ERR_* on write error or invalid arguments.
 */
int imd_write_file_header(FILE* fout, const char* version_string);

/**
 * Writes the comment block and the terminating EOF marker (0x1A).
 * @param fout Output file stream.
 * @param comment Pointer to the comment string (can be NULL or empty).
 * @param comment_len Length of the comment string (used only if comment is not NULL).
 * @return 0 on success, negative IMD_ERR_* on write error.
 */
int imd_write_comment_block(FILE* fout, const char* comment, size_t comment_len);


/* --- Track Handling --- */

/**
 * Gets the sector size in bytes based on the track's sector_size_code.
 * @param track Pointer to the ImdTrackInfo structure containing the sector_size_code. Must not be NULL.
 * @return Sector size in bytes, or 0 if code is invalid or track is NULL.
 */
uint32_t imd_get_sector_size(const ImdTrackInfo* track);

/**
 * Allocates the data buffer within an ImdTrackInfo structure based on
 * num_sectors and sector_size. Sets track->data and track->data_size.
 * Assumes track->num_sectors and track->sector_size are already set.
 * Must be freed later using imd_free_track_data().
 * @param track Pointer to the ImdTrackInfo structure. Must not be NULL.
 * @return 0 on success, IMD_ERR_ALLOC on memory allocation failure, IMD_ERR_INVALID_ARG if track is NULL or size is invalid.
 */
int imd_alloc_track_data(ImdTrackInfo* track);

/**
 * Gets the internal sector size lookup table used by the library.
 * @param count Optional pointer to store the number of elements in the table. Can be NULL.
 * @return Pointer to the constant sector size lookup table (uint32_t array).
 */
const uint32_t* imd_get_sector_size_lookup(size_t* count);

/**
 * Loads a single track (header, maps, and data) from an IMD file stream.
 * Allocates memory for sector data within the track structure.
 * Memory must be freed using imd_free_track_data().
 * @param fimd Input file stream, positioned at the start of a track record.
 * @param track Pointer to the ImdTrackInfo structure to fill. Must not be NULL.
 * @param fill_byte Byte value used to fill the data buffer for sectors marked as unavailable (IMD_SDR_UNAVAILABLE).
 * @return 1 if a track was loaded successfully.
 * @return 0 if EOF was reached cleanly at the start of a track.
 * @return Negative value (IMD_ERR_*) on error (e.g., unexpected EOF, invalid data, memory allocation failure).
 */
int imd_load_track(FILE* fimd, ImdTrackInfo* track, uint8_t fill_byte);

/**
 * Reads only the track header and maps from an IMD file stream.
 * Does NOT allocate or read sector data. Sets track->data to NULL and track->loaded to 0.
 * Skips over the sector data records in the file stream.
 * Useful for quickly scanning track metadata.
 * @param fimd Input file stream, positioned at the start of a track record.
 * @param track Pointer to the ImdTrackInfo structure to fill (header/maps only). Must not be NULL.
 * @return 1 if a track header was read successfully.
 * @return 0 if EOF was reached cleanly at the start of a track.
 * @return Negative value (IMD_ERR_*) on error (e.g., unexpected EOF, invalid data, skip error).
 */
int imd_read_track_header(FILE* fimd, ImdTrackInfo* track);

/**
 * Reads track header, maps, and sector flags from an IMD file stream.
 * Does NOT allocate or read sector data. Sets track->data to NULL and track->loaded to 0.
 * Skips over the sector data records in the file stream based on flags read.
 * Populates the sflag array in the track structure.
 * Useful for scanning metadata including sector status.
 * @param fimd Input file stream, positioned at the start of a track record.
 * @param track Pointer to the ImdTrackInfo structure to fill (header/maps/flags only). Must not be NULL.
 * @return 1 if a track header/flags were read successfully.
 * @return 0 if EOF was reached cleanly at the start of a track.
 * @return Negative value (IMD_ERR_*) on error (e.g., unexpected EOF, invalid data, skip error).
 */
int imd_read_track_header_and_flags(FILE* fimd, ImdTrackInfo* track);

/**
 * Checks if a specific track (cylinder/head) in an IMD file contains any "valid" sectors.
 * A sector is considered valid if its base type is not IMD_SDR_UNAVAILABLE.
 * This function scans the file without loading full track data.
 * Preserves the original file position on success or failure.
 * @param fimd Input file stream. Assumed to be open and valid. Must be seekable.
 * @param cyl Target cylinder number.
 * @param head Target head number.
 * @return 1 if the track exists and has at least one valid (non-unavailable) sector.
 * @return 0 if the track exists but only contains unavailable sectors, or has 0 sectors.
 * @return IMD_ERR_TRACK_NOT_FOUND if the track does not exist in the file.
 * @return Other negative IMD_ERR_* code on file I/O errors.
 */
int imd_track_has_valid_sectors(FILE* fimd, uint8_t cyl, uint8_t head);

/**
 * Frees the sector data buffer allocated within an ImdTrackInfo structure by imd_load_track().
 * Also resets data pointer, data size, and loaded flag in the structure.
 * Safe to call even if track->data is already NULL.
 * @param track Pointer to the ImdTrackInfo structure whose data should be freed. Can be NULL.
 */
void imd_free_track_data(ImdTrackInfo* track);

/**
 * Writes a track from memory to an output file in IMD format.
 * Applies processing options like compression, flag forcing, mode translation,
 * and interleaving to the track data *before* writing.
 * The input track structure is not modified unless interleaving occurs (temporary internal modification).
 * @param fout Output file stream.
 * @param track Pointer to the loaded ImdTrackInfo structure containing track data. Must be loaded (track->loaded == 1).
 * @param opts Pointer to ImdWriteOpts structure with processing options. Must not be NULL.
 * @return 0 on success, negative IMD_ERR_* code on error (write error, allocation error, invalid args).
 */
int imd_write_track_imd(FILE* fout, ImdTrackInfo* track, const ImdWriteOpts* opts);

/**
 * Writes the raw sector data of a track (potentially reordered by interleave option)
 * to an output file in flat binary format. No IMD formatting is written.
 * The input track structure is not modified unless interleaving occurs (temporary internal modification).
 * @param fout Output file stream.
 * @param track Pointer to the loaded ImdTrackInfo structure containing track data. Must be loaded.
 * @param opts Pointer to ImdWriteOpts structure (only interleave_factor is used). Must not be NULL.
 * @return 0 on success, negative IMD_ERR_* code on error (write error, allocation error, invalid args).
 */
int imd_write_track_bin(FILE* fout, ImdTrackInfo* track, const ImdWriteOpts* opts);

/**
 * Calculates the best guess interleave factor for a track based on its sector numbering map (smap).
 * It determines the most frequent distance between logically sequential sectors in the physical layout.
 * @param track Pointer to the loaded ImdTrackInfo structure. Must be loaded.
 * @return The calculated interleave factor (typically >= 1). Returns 1 if track has < 2 sectors or on error.
 */
int imd_calculate_best_interleave(ImdTrackInfo* track);

/**
 * Reorders track maps (smap, cmap, hmap, sflag) and the sector data buffer
 * within the ImdTrackInfo structure based on a specified interleave factor.
 * Modifies the track structure in place. Requires track->loaded == 1.
 * @param track Pointer to the ImdTrackInfo structure to modify. Must be loaded.
 * @param interleave_factor The desired interleave factor (must be >= 1).
 * @return 0 on success, IMD_ERR_ALLOC on memory allocation failure, IMD_ERR_INVALID_ARG for invalid arguments.
 */
int imd_apply_interleave(ImdTrackInfo* track, int interleave_factor);

/**
 * Checks if all bytes in a buffer have the same value (uniform).
 * @param data Pointer to the data buffer.
 * @param size Size of the buffer in bytes.
 * @param fill_byte_out Pointer to store the fill byte if the buffer is uniform. Must not be NULL.
 * @return 1 if all bytes are the same (or size is 0), 0 otherwise or if data/fill_byte_out is NULL.
 */
int imd_is_uniform(const uint8_t* data, size_t size, uint8_t* fill_byte_out);

/**
 * Public helper function to write a specified number of bytes to a file stream.
 * Provides direct access to the internal byte writing logic.
 * @param buffer Pointer to the buffer to write from.
 * @param size Number of bytes to write.
 * @param file File stream to write to.
 * @return 0 on success, negative IMD_ERR_* on write error.
 */
int imd_write_bytes(const void* buffer, size_t size, FILE* file);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBIMD_H */

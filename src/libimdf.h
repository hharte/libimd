/*
 * In-Memory ImageDisk File Library Definitions.
 *
 * Provides functions to open, access, and modify IMD image files
 * by maintaining the entire image structure in memory.
 * Built upon libimd.
 *
 * www.github.com/hharte/libimd
 *
 * Copyright (c) 2025, Howard M. Harte
 */

#ifndef LIBIMDF_H
#define LIBIMDF_H

#include "libimd.h" /* Requires libimd */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Error Codes for libimdf --- */
#define IMDF_ERR_OK              0  /* Success */
#define IMDF_ERR_WRITE_PROTECTED -100 /* Operation failed: Image is write-protected */
#define IMDF_ERR_GEOMETRY        -101 /* Operation failed: Exceeds geometry limits */
#define IMDF_ERR_NOT_FOUND       -102 /* Track or sector not found */
#define IMDF_ERR_ALLOC           -103 /* Memory allocation failure */
#define IMDF_ERR_IO              -104 /* File I/O error (read/write/seek) */
#define IMDF_ERR_INVALID_ARG     -105 /* Invalid argument passed to function */
#define IMDF_ERR_SECTOR_SIZE     -106 /* Invalid sector size specified or buffer mismatch */
#define IMDF_ERR_BUFFER_SIZE     -107 /* Provided buffer size is insufficient */
#define IMDF_ERR_UNAVAILABLE     -108 /* Target sector is marked as unavailable */
#define IMDF_ERR_LIBIMD_ERR      -109 /* Internal error originating from libimd */
#define IMDF_ERR_ALREADY_OPEN    -110 /* File handle already associated with an image */
#define IMDF_ERR_CANNOT_OPEN     -111 /* Cannot open the specified image file */

/* --- Data Structures --- */

/* Structure representing an open IMD file in memory */
typedef struct ImdImageFile ImdImageFile; /* Opaque structure */

/* --- Public Function Prototypes --- */

/* --- Image Handling --- */

/**
 * Opens an IMD image file and loads its entire structure into memory.
 * @param path Path to the IMD file.
 * @param read_only If non-zero, opens the file read-only and prevents modifications.
 * @param imdf_out Pointer to store the allocated ImdImageFile handle on success.
 * @return IMDF_ERR_OK on success, negative IMDF_ERR_* code on failure.
 */
int imdf_open(const char* path, int read_only, ImdImageFile** imdf_out);

/**
 * Closes an open IMD image file, frees all associated memory, and closes the file handle.
 * @param imdf Pointer to the ImdImageFile handle obtained from imdf_open. Can be NULL.
 */
void imdf_close(ImdImageFile* imdf);

/* --- Geometry --- */

/**
 * Sets the physical geometry limits for the image.
 * These limits are used to validate cylinder/head/sector parameters in other functions.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param max_cyl Maximum cylinder number (inclusive, 0-based). Set to 0xFF if unused.
 * @param max_head Maximum head number (inclusive, 0-based, typically 0 or 1). Set to 0xFF if unused.
 * @param max_spt Maximum sectors per track (optional, 0 if unused). Set to 0xFF if unused.
 * @return IMDF_ERR_OK on success, IMDF_ERR_INVALID_ARG if imdf is NULL.
 */
int imdf_set_geometry(ImdImageFile* imdf, uint8_t max_cyl, uint8_t max_head, uint8_t max_spt);

/**
 * Gets the physical geometry limits set for the image.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param max_cyl_out Pointer to store the maximum cylinder number. Can be NULL.
 * @param max_head_out Pointer to store the maximum head number. Can be NULL.
 * @param max_spt_out Pointer to store the maximum sectors per track. Can be NULL.
 * @return IMDF_ERR_OK on success, IMDF_ERR_INVALID_ARG if imdf is NULL.
 */
int imdf_get_geometry(ImdImageFile* imdf, uint8_t* max_cyl_out, uint8_t* max_head_out, uint8_t* max_spt_out);

/* --- Write Protection --- */

/**
 * Sets the write-protection status for the image.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param protect If non-zero, enables write protection. If zero, disables it (if image was opened r/w).
 * @return IMDF_ERR_OK on success, IMDF_ERR_INVALID_ARG if imdf is NULL,
 * IMDF_ERR_WRITE_PROTECTED if trying to disable protection on a read-only image.
 */
int imdf_set_write_protect(ImdImageFile* imdf, int protect);

/**
 * Gets the current write-protection status for the image.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param protect_out Pointer to store the write-protection status (1 if protected, 0 otherwise).
 * @return IMDF_ERR_OK on success, IMDF_ERR_INVALID_ARG if imdf or protect_out is NULL.
 */
int imdf_get_write_protect(ImdImageFile* imdf, int* protect_out);

/* --- Metadata Access --- */

/**
 * Gets a pointer to the parsed IMD file header information.
 * @param imdf Pointer to the ImdImageFile handle.
 * @return Pointer to the constant ImdHeaderInfo structure, or NULL if imdf is NULL.
 * Do not modify the returned structure.
 */
const ImdHeaderInfo* imdf_get_header_info(const ImdImageFile* imdf);

/**
 * Gets the comment block read from the IMD file.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param comment_len_out Optional pointer to store the length of the comment (excluding null terminator). Can be NULL.
 * @return Pointer to the constant comment string (null-terminated), or NULL if imdf is NULL or no comment exists.
 * Do not modify the returned string.
 */
const char* imdf_get_comment(const ImdImageFile* imdf, size_t* comment_len_out);

/**
 * Gets the total number of tracks loaded from the IMD image.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param num_tracks_out Pointer to store the number of tracks.
 * @return IMDF_ERR_OK on success, IMDF_ERR_INVALID_ARG if imdf or num_tracks_out is NULL.
 */
int imdf_get_num_tracks(const ImdImageFile* imdf, size_t* num_tracks_out);

/**
 * Gets a pointer to the information for a specific track by its index.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param track_index The index of the track (0 to num_tracks-1).
 * @return Pointer to the constant ImdTrackInfo structure for the requested track,
 * or NULL if imdf is NULL or track_index is out of bounds.
 * Do not modify the returned structure directly; use read/write functions.
 */
const ImdTrackInfo* imdf_get_track_info(const ImdImageFile* imdf, size_t track_index);

/**
 * Finds the index of a track matching the specified cylinder and head.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param cyl Target cylinder number.
 * @param head Target head number.
 * @param track_index_out Pointer to store the found track index.
 * @return IMDF_ERR_OK if found, IMDF_ERR_NOT_FOUND if not found,
 * IMDF_ERR_INVALID_ARG if imdf or track_index_out is NULL.
 */
int imdf_find_track_by_ch(const ImdImageFile* imdf, uint8_t cyl, uint8_t head, size_t* track_index_out);


/* --- Sector Access --- */

/**
 * Reads data from a specific sector into the provided buffer.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param cyl Cylinder number of the target sector.
 * @param head Head number of the target sector.
 * @param logical_sector_id The logical sector ID (from SMAP) to read.
 * @param buffer Pointer to the buffer where sector data will be copied.
 * @param buffer_size Size of the provided buffer in bytes.
 * @return IMDF_ERR_OK on success.
 * @return IMDF_ERR_INVALID_ARG if imdf or buffer is NULL.
 * @return IMDF_ERR_GEOMETRY if cyl/head exceeds limits.
 * @return IMDF_ERR_NOT_FOUND if the track or logical sector ID does not exist.
 * @return IMDF_ERR_UNAVAILABLE if the sector is marked as unavailable (type 0x00).
 * @return IMDF_ERR_BUFFER_SIZE if buffer_size is less than the actual sector size.
 */
int imdf_read_sector(ImdImageFile* imdf, uint8_t cyl, uint8_t head, uint8_t logical_sector_id, uint8_t* buffer, size_t buffer_size);

/**
 * Writes data from a buffer to a specific sector.
 * If the target sector belongs to a track marked as compressed, the entire track
 * will be rewritten to the file decompressed. Changes are persisted immediately.
 * @param imdf Pointer to the ImdImageFile handle.
 * @param cyl Cylinder number of the target sector.
 * @param head Head number of the target sector.
 * @param logical_sector_id The logical sector ID (from SMAP) to write.
 * @param buffer Pointer to the buffer containing the data to write.
 * @param buffer_size Size of the data in the buffer. Must match the sector size.
 * @return IMDF_ERR_OK on success.
 * @return IMDF_ERR_INVALID_ARG if imdf or buffer is NULL.
 * @return IMDF_ERR_WRITE_PROTECTED if the image is write-protected.
 * @return IMDF_ERR_GEOMETRY if cyl/head exceeds limits.
 * @return IMDF_ERR_NOT_FOUND if the track or logical sector ID does not exist.
 * @return IMDF_ERR_SECTOR_SIZE if buffer_size does not match the track's sector size.
 * @return IMDF_ERR_IO on file write error during persistence.
 * @return IMDF_ERR_ALLOC on memory allocation failure during rewrite.
 * @return IMDF_ERR_LIBIMD_ERR on internal libimd errors during rewrite.
 */
int imdf_write_sector(ImdImageFile* imdf, uint8_t cyl, uint8_t head, uint8_t logical_sector_id, const uint8_t* buffer, size_t buffer_size);


/* --- Track Writing --- */

/**
 * Writes (or overwrites) an entire track.
 * Creates the track if it doesn't exist, inserting it in C/H order.
 * If overwriting, the existing track data is replaced. Sector size can be changed.
 * All sectors in the written track will be initially marked as 'Normal' (IMD_SDR_NORMAL).
 * Changes are persisted immediately.
 *
 * @param imdf Pointer to the ImdImageFile handle.
 * @param cyl Cylinder number for the track.
 * @param head Head number for the track.
 * @param num_sectors Number of sectors for the new/updated track (0-255).
 * @param sector_size Size in bytes for each sector (must be a valid IMD size like 128, 256, etc.).
 * @param fill_byte Byte value used to fill the data buffer for all sectors.
 * @param smap Pointer to a sector numbering map (array of num_sectors bytes).
 * If NULL, a sequential map (1 to num_sectors) will be generated, and optional maps (cmap, hmap) will be omitted.
 * @param cmap Optional pointer to a cylinder numbering map (array of num_sectors bytes).
 * If NULL, the CMAP flag will not be set, and no cylinder map will be stored. Requires non-NULL smap.
 * @param hmap Optional pointer to a head numbering map (array of num_sectors bytes).
 * If NULL, the HMAP flag will not be set, and no head map will be stored. Requires non-NULL smap.
 * @return IMDF_ERR_OK on success.
 * @return IMDF_ERR_INVALID_ARG if imdf is NULL or required args are missing/invalid.
 * @return IMDF_ERR_WRITE_PROTECTED if the image is write-protected.
 * @return IMDF_ERR_GEOMETRY if cyl/head exceeds limits or num_sectors exceeds LIBIMD_MAX_SECTORS_PER_TRACK.
 * @return IMDF_ERR_SECTOR_SIZE if the specified sector_size is invalid.
 * @return IMDF_ERR_ALLOC on memory allocation failure.
 * @return IMDF_ERR_IO on file write error during persistence.
 * @return IMDF_ERR_LIBIMD_ERR on internal libimd errors during persistence.
 */
int imdf_write_track(ImdImageFile* imdf,
                     uint8_t cyl,
                     uint8_t head,
                     uint8_t num_sectors,
                     uint32_t sector_size,
                     uint8_t fill_byte,
                     const uint8_t* smap,
                     const uint8_t* cmap, /* Added */
                     const uint8_t* hmap  /* Added */
                     );

/**
 * @brief Finds the physical index of a sector given its logical ID.
 * @param track Pointer to the track information.
 * @param logical_sector_id The logical sector ID to find.
 * @return The physical index of the sector, or -1 if not found or track is NULL.
 */
int find_sector_index_internal(const ImdTrackInfo* track, uint8_t logical_sector_id);

/**
 * @brief Gets the IMD sector size code for a given sector size in bytes.
 * @param sector_size The sector size in bytes (e.g., 128, 256, 512).
 * @param code_out Pointer to store the resulting sector size code.
 * @return 0 on success, -1 if the sector size is not found in the lookup table.
 */
int get_sector_size_code(uint32_t sector_size, uint8_t* code_out);

/**
 * Formats (or re-formats) an entire track with specific sector numbering.
 * Creates the track if it doesn't exist, inserting it in C/H order.
 * If overwriting, the existing track data is replaced.
 * The sector numbering (smap) is generated according to the first_sector_id
 * and interleave parameters, with sector IDs wrapping around num_sectors.
 * All sectors in the formatted track will be filled with fill_byte and
 * initially marked to allow compression if the fill_byte results in uniform data.
 * Changes are persisted immediately to the file.
 *
 * @param imdf Pointer to the ImdImageFile handle.
 * @param cyl Physical cylinder number for the track.
 * @param head Physical head number for the track.
 * @param mode The IMD mode (e.g., IMD_MODE_MFM_250) for this track.
 * @param num_sectors Number of sectors for the track (0-255).
 * @param sector_size Size in bytes for each sector (must be a valid IMD size).
 * @param first_sector_id The logical ID of the first sector to be written in the track's smap.
 * Sector IDs are typically 1-based and go up to num_sectors.
 * @param interleave The interleave factor used to determine the sequence of logical
 * sector IDs laid out on the physical track.
 * @param skew The offset from physical sector 0.
 * @param fill_byte Byte value used to fill the data buffer for all sectors.
 * @return IMDF_ERR_OK on success.
 * @return IMDF_ERR_INVALID_ARG if imdf is NULL or parameters are invalid (e.g. num_sectors too large, invalid sector_size, first_sector_id out of range).
 * @return IMDF_ERR_WRITE_PROTECTED if the image is write-protected.
 * @return IMDF_ERR_GEOMETRY if cyl/head exceeds limits.
 * @return IMDF_ERR_SECTOR_SIZE if the specified sector_size is invalid.
 * @return IMDF_ERR_ALLOC on memory allocation failure.
 * @return IMDF_ERR_IO on file write error during persistence.
 * @return IMDF_ERR_LIBIMD_ERR on internal libimd errors.
 */
int imdf_format_track(ImdImageFile* imdf,
    uint8_t cyl,
    uint8_t head,
    uint8_t mode,
    uint8_t num_sectors,
    uint32_t sector_size,
    uint8_t first_sector_id,
    int interleave,
    int skew,
    uint8_t fill_byte);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBIMDF_H */

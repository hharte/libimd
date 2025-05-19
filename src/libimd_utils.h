/*
 * Common Utilities for libimd.
 * Error Reporting and String Manipulation.
 *
 * www.github.com/hharte/libimd
 *
 * Copyright (c) 2025, Howard M. Harte
 */

#ifndef LIBIMD_UTILS_H
#define LIBIMD_UTILS_H

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Reporting Levels --- */
typedef enum {
    IMD_REPORT_LEVEL_DEBUG = 0, /* For detailed debugging (verbose only) */
    IMD_REPORT_LEVEL_INFO,      /* General information (verbose only) */
    IMD_REPORT_LEVEL_WARNING,   /* Warnings (suppressed by quiet) */
    IMD_REPORT_LEVEL_ERROR      /* Errors (always shown) */
} ImdReportLevel;

/* --- Verbosity/Quiet Control --- */

/**
 * @brief Sets the reporting verbosity level based on quiet/verbose flags.
 * Must be called once by the application (e.g., in main) before using
 * reporting functions.
 * @param quiet Non-zero suppresses warnings and info messages.
 * @param verbose Non-zero enables info and debug messages.
 */
void imd_set_verbosity(int quiet, int verbose);

/* --- Reporting Functions --- */

/**
 * @brief Reports a message (error, warning, info, debug) based on the level
 * and current verbosity settings. Uses printf-style formatting.
 * Error messages are printed to stderr, others typically to stdout or stderr
 * depending on the level.
 * @param level The reporting level (IMD_REPORT_LEVEL_*).
 * @param format The printf-style format string.
 * @param ... Variable arguments for the format string.
 */
void imd_report(ImdReportLevel level, const char* format, ...);

/**
 * @brief Reports an error message to stderr and exits the program
 * with EXIT_FAILURE. Always printed regardless of verbosity settings.
 * @param format The printf-style format string.
 * @param ... Variable arguments for the format string.
 */
void imd_report_error_exit(const char* format, ...);

/* --- String Utilities --- */

/**
 * @brief Returns a pointer to the base name (filename part) of a path string.
 * Handles both '/' and '\' directory separators. Returns pointer within the original string.
 * @param path The full path string.
 * @return Pointer to the start of the base name within the path string, or the original path if no separators are found, or NULL if path is NULL.
 */
const char* imd_get_basename(const char* path);

/**
 * @brief Converts a hexadecimal character ('0'-'9', 'a'-'f', 'A'-'F') to its integer value (0-15).
 * @param c The character to convert.
 * @return The integer value (0-15), or -1 if the character is not a valid hex digit.
 */
int imd_ctoh(int c);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBIMD_UTILS_H */

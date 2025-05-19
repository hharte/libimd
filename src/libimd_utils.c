/*
 * Common Utilities for libimd.
 * Implementation of Error Reporting and String Manipulation.
 *
 * www.github.com/hharte/libimd
 *
 * Copyright (c) 2025, Howard M. Harte
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "libimd.h"

/* --- Static Global Variables for Reporting State --- */
static int g_quiet_mode = 0;
static int g_verbose_mode = 0;

/* --- Verbosity/Quiet Control Implementation --- */

void imd_set_verbosity(int quiet, int verbose) {
    g_quiet_mode = quiet;
    g_verbose_mode = verbose;
}

/* --- Reporting Function Implementation --- */

void imd_report(ImdReportLevel level, const char* format, ...) {
    va_list args;
    FILE* output_stream = stderr; /* Default to stderr for errors/warnings */
    const char* prefix = "";
    int print_message = 0;

    switch (level) {
        case IMD_REPORT_LEVEL_DEBUG:
            if (g_verbose_mode) {
                prefix = "Debug: ";
                output_stream = stdout; /* Debug often goes to stdout */
                print_message = 1;
            }
            break;
        case IMD_REPORT_LEVEL_INFO:
            if (g_verbose_mode) {
                prefix = ""; /* Info often doesn't need a prefix */
                output_stream = stdout;
                print_message = 1;
            }
            break;
        case IMD_REPORT_LEVEL_WARNING:
            if (!g_quiet_mode) {
                prefix = "Warning: ";
                print_message = 1;
            }
            break;
        case IMD_REPORT_LEVEL_ERROR:
            /* Errors are always printed, regardless of quiet/verbose */
            prefix = "Error: ";
            print_message = 1;
            break;
        default:
            /* Unknown level, treat as warning? Or error? */
            if (!g_quiet_mode) {
                prefix = "Unknown Msg: ";
                print_message = 1;
            }
            break;
    }

    if (print_message) {
        fprintf(output_stream, "%s", prefix);
        va_start(args, format);
        vfprintf(output_stream, format, args);
        va_end(args);
        fprintf(output_stream, "\n");
        fflush(output_stream); /* Ensure message is seen immediately */
    }
}

void imd_report_error_exit(const char* format, ...) {
    va_list args;
    fprintf(stderr, "Error: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
    exit(EXIT_FAILURE);
}

/* --- String Utilities Implementation --- */

/* Implementation moved from libimd.c */
const char* imd_get_basename(const char* path) {
    if (path == NULL) {
        return NULL;
    }
    const char* basename = path;
    const char* p = path;
    /* Iterate through the string */
    while (*p) {
        /* If a separator is found, the character *after* it is the potential start of the basename */
        if (*p == '/' || *p == '\\') {
            basename = p + 1;
        }
        p++;
    }
    return basename;
}

/* Implementation moved from libimd.c */
int imd_ctoh(int c) {
    if (isdigit(c)) { /* Use isdigit for 0-9 */
        return c - '0';
    }
    else {
        c = tolower(c); /* Convert to lowercase for a-f */
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
    }
    return -1; /* Invalid hex character */
}

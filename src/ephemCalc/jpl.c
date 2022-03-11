// jpl.c
// 
// -------------------------------------------------
// Copyright 2015-2020 Dominic Ford
//
// This file is part of EphemerisCompute.
//
// EphemerisCompute is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// EphemerisCompute is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with EphemerisCompute.  If not, see <http://www.gnu.org/licenses/>.
// -------------------------------------------------

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include <gsl/gsl_math.h>

#include "coreUtils/asciiDouble.h"
#include "coreUtils/errorReport.h"
#include "coreUtils/strConstants.h"

#include "listTools/ltDict.h"
#include "listTools/ltMemory.h"

#include "settings/settings.h"

#include "jpl.h"
#include "orbitalElements.h"
#include "magnitudeEstimate.h"

//! The number of the JPL ephemeris we are using: DE405
static int JPL_EphemNumber = 405;

//! The starting year of the DE405 ephemeris
static int JPL_ASCII_first = 1600;

//! The end year of the DE405 ephemeris
static int JPL_ASCII_last = 2200;

//! The number of years within each discrete file
static int JPL_ASCII_step = 20;

//! Storage for data read from DE405
static double JPL_EphemStart = 0; // The Julian day number of the start of the DE405 ephemeris
static double JPL_EphemEnd = 0; // The Julian day number of the end of the DE405 ephemeris
static double JPL_EphemStep = 0; // The number of days represented by each data block in DE405 (32 days)
static int JPL_EphemArrayLen = 0; // Length of a data record containing Chebyshev coefficients for all planets for time interval JPL_EphemStep
static int *JPL_ShapeData = NULL; // The 13x3 shape array defined in GROUP 1050 in the header file
static dict *JPL_EphemVars = NULL; // The metadata variables about the ephemeris, defined in GROUP 1040/1041
static int JPL_EphemArrayRecords = 0; // The number of blocks needed to go from EphemStart to EphemEnd at step size EphemStep

static int JPL_EphemData_offset = -1; // The offset of the start of the ephmeris binary data from the start of the binary file
static FILE *JPL_EphemFile = NULL; // File pointer used to read binary data from DE405 (we don't read whole binary ephemeris into memory)

static double *JPL_EphemData = NULL; // Buffer to hold the ephemeris data, as we load it
static unsigned char *JPL_EphemData_items_loaded = NULL; // Record of which ephemeris data records we have loaded

static double JPL_AU = 0.0; // astronomical unit, measured in km


//! JPL_ReadBinaryData - restore DE405 from a binary dump of the data in <data/dcfbinary.405>, to save parsing
//! original files every time we are run.

int JPL_ReadBinaryData() {
    char fname[FNAME_LENGTH];

    // Work out the filename of the binary file that we are to open
    snprintf(fname, FNAME_LENGTH, "%s/../data/dcfbinary.%d", SRCDIR, JPL_EphemNumber);
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "Trying to fetch binary data from file <%s>.", fname);
        ephem_log(temp_err_string);
    }

    // Open binary data
    JPL_EphemFile = fopen(fname, "r");
    if (JPL_EphemFile == NULL) return 1; // Failed to open binary file

    // Read headers to binary file
    dcffread((void *) &JPL_EphemStart, sizeof(double), 1, JPL_EphemFile);
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "JPL_EphemStart        = %10f", JPL_EphemStart);
        ephem_log(temp_err_string);
    }
    dcffread((void *) &JPL_EphemEnd, sizeof(double), 1, JPL_EphemFile);
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "JPL_EphemEnd          = %10f", JPL_EphemEnd);
        ephem_log(temp_err_string);
    }
    dcffread((void *) &JPL_EphemStep, sizeof(double), 1, JPL_EphemFile);
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "JPL_EphemStep         = %10f", JPL_EphemStep);
        ephem_log(temp_err_string);
    }
    dcffread((void *) &JPL_AU, sizeof(double), 1, JPL_EphemFile);
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "JPL_AU                = %10f", JPL_AU);
        ephem_log(temp_err_string);
    }
    dcffread((void *) &JPL_EphemArrayLen, sizeof(int), 1, JPL_EphemFile);
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "JPL_EphemArrayLen     = %10d", JPL_EphemArrayLen);
        ephem_log(temp_err_string);
    }
    dcffread((void *) &JPL_EphemArrayRecords, sizeof(int), 1, JPL_EphemFile);
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "JPL_EphemArrayRecords = %10d", JPL_EphemArrayRecords);
        ephem_log(temp_err_string);
    }

    JPL_ShapeData = (int *) lt_malloc(13 * 3 * sizeof(int));
    if (JPL_ShapeData == NULL) {
        ephem_fatal(__FILE__, __LINE__, "Malloc fail.");
        exit(1);
    }

    // Read shape data array
    dcffread((void *) JPL_ShapeData, sizeof(int), 13 * 3, JPL_EphemFile);

    // We have now reached the actual ephemeris data. We don't load this into RAM since it is large and this would
    // take time. Instead store a pointer to the offset of the start of the ephemeris from the beginning of file.
    JPL_EphemData_offset = (int) ftell(JPL_EphemFile);

    // Allocate memory to use to store ephemeris, as we load it
    JPL_EphemData = (double *) lt_malloc(JPL_EphemArrayLen * JPL_EphemArrayRecords * sizeof(double));

    // Allocate array to record which blocks we have already loaded from disk
    JPL_EphemData_items_loaded = (unsigned char *) lt_malloc(JPL_EphemArrayRecords * sizeof(unsigned char));
    memset(JPL_EphemData_items_loaded, 0, JPL_EphemArrayRecords);

    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "Data file successfully opened.");
        ephem_log(temp_err_string);
    }

    // Success
    return 0;
}

//! JPL_DumpBinaryData - dump contents of DE405 to a binary dump in <data/dcfbinary.405>, to save parsing
//! original files every time we are run.

void JPL_DumpBinaryData() {
    FILE *output;
    char fname[FNAME_LENGTH];

    snprintf(fname, FNAME_LENGTH, "%s/../data/dcfbinary.%d", SRCDIR, JPL_EphemNumber);
    if (DEBUG) {
        sprintf(temp_err_string, "Dumping binary data to file <%s>.", fname);
        ephem_log(temp_err_string);
    }
    output = fopen(fname, "w");
    if (output == NULL) return; // FAIL
    fwrite((void *) &JPL_EphemStart, sizeof(double), 1, output);
    fwrite((void *) &JPL_EphemEnd, sizeof(double), 1, output);
    fwrite((void *) &JPL_EphemStep, sizeof(double), 1, output);
    fwrite((void *) &JPL_AU, sizeof(double), 1, output);
    fwrite((void *) &JPL_EphemArrayLen, sizeof(int), 1, output);
    fwrite((void *) &JPL_EphemArrayRecords, sizeof(int), 1, output);
    fwrite((void *) JPL_ShapeData, sizeof(int), 13 * 3, output);
    fwrite((void *) JPL_EphemData, sizeof(double), JPL_EphemArrayLen * JPL_EphemArrayRecords, output);
    fclose(output);
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "Data successfully dumped.");
        ephem_log(temp_err_string);
    }
}

//! jpl_readData - Read the data contained in the original DE405 files

void jpl_readAsciiData() {
    char fname[FNAME_LENGTH], line[FNAME_LENGTH], *lineptr, key[FNAME_LENGTH];

    FILE *input = NULL;  // The ASCII file we are reading the ephemeris from
    int year = -1;  // The year number in the filename of the ephemeris file we are reading (advances in 20 year steps)
    int state = -1;  // The last GROUP number header we passed; different blocks of data have different GROUP numbers
    int var_dict_len = -1;  // The number of metadata variables set in GROUP 1040, in the header of the ephemeris
    int first = 0;  // Boolean flag indicating whether this is the first line of the current GROUP
    int malloced_data_len = -1;  // The number of bytes allocated to hold the ephemeris in <JPL_EphemData>
    int i = 0;  // general purpose counter
    int pos = 0;  // The current write position in the array <JPL_EphemData>
    int count = 0;  // Count the floating point numbers we've read in the current ephemeris block
    int ignore = 0;  // Boolean flag indicating whether we're ignoring a data block because it repeats data for the
    // time span we've already passed
    double *var_val = NULL; // Array of doubles for holding the values of the metadata variables in GROUP 1040/1041
    double jd_min = 0;  // The Julian Day number at the start of the current ephemeris block

    // Try and read the ephemeris from binary files. Only proceed with parsing the original files if binary files
    // don't exist.
    if (JPL_ReadBinaryData() == 0) return;

    // Logging message to report that we are parsing the DE405 files
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "Beginning to read JPL epemeris DE%d.", JPL_EphemNumber);
        ephem_log(temp_err_string);
    }

    // The header file, <data/header.405>, contains global information about the ephemeris
    snprintf(fname, FNAME_LENGTH, "%s/../data/header.%d", SRCDIR, JPL_EphemNumber);

    while (1) {
        // If we don't currently have an ephemeris file with readable data, we need to open one
        if ((input == NULL) || (feof(input)) || (ferror(input))) {
            // If we already have an ephemeris file open, close it
            if (input != NULL) fclose(input);

            // If we've reached the end of the time span of DE405, we're finished
            if (year > JPL_ASCII_last) break;

            // If we've not started reading yet, start at the beginning, otherwise advance 20 years
            if (year == -1) year = JPL_ASCII_first;
            else year += JPL_ASCII_step;

            // Log message that we are opening a new ephemeris file
            if (DEBUG) {
                snprintf(temp_err_string, FNAME_LENGTH, "Opening file <%s>", fname);
                ephem_log(temp_err_string);
            }

            // Open the file -- first time around the header files; subsequently, the ephemeris itself
            input = fopen(fname, "r");
            if (input == NULL) {
                ephem_fatal(__FILE__, __LINE__, "Could not open ephemeris file.");
                exit(1);
            }

            // Populate <fname> with the filename of the next ephemeris file to read
            // The ephemeris data is contained in files <data/ascp????.405>, where ???? is the start year
            snprintf(fname, FNAME_LENGTH, "%s/../data/ascp%d.%d", SRCDIR, year, JPL_EphemNumber);
        }

        // Read a line of data from the ephemeris file
        file_readline(input, line);
        str_strip(line, line);

        // Ignore blank lines
        if (line[0] == '\0') continue;

        // The first line of the header file contains the length of the records in the ephemeris files (NCOEFF=...)
        if (strncmp(line, "KSIZE=", 5) == 0) {
            //KSIZE =
            lineptr = next_word(line);
            lineptr = next_word(lineptr);

            //NCOEFF =
            lineptr = next_word(lineptr);
            JPL_EphemArrayLen = (int) get_float(lineptr, NULL) + 2; // Two extra floats are JD limits of time step
            if (DEBUG) {
                snprintf(temp_err_string, FNAME_LENGTH, "Each record of length %d floats.", JPL_EphemArrayLen);
                ephem_log(temp_err_string);
            }
            continue;
        }

        // The header file is divided into parts, each with a GROUP number. We set <state> to the current group number
        if (strncmp(line, "GROUP", 5) == 0) {
            first = 1;  // This is the first line of this group
            pos = 0;
            count = 0;
            lineptr = next_word(line);
            state = (int) get_float(lineptr, NULL);  // Set state to the new GROUP number
            if (DEBUG) {
                snprintf(temp_err_string, FNAME_LENGTH, "Entering GROUP %d.", state);
                ephem_log(temp_err_string);
            }

            if (state == 1040) {
                // Entering group 1040, which defines a number of variables
                // Before we start, we create an associative array to put them in
                JPL_EphemVars = dictInit(HASHSIZE_SMALL);
                if (JPL_EphemVars == NULL) {
                    ephem_fatal(__FILE__, __LINE__, "Malloc fail.");
                    exit(1);
                }
            } else if (state == 1050) {
                // Entering group 1050, which defines the shape array
                // Before we start we need to allocate storage for the 13x3 shape array
                JPL_ShapeData = (int *) lt_malloc(13 * 3 * sizeof(int));
                if (JPL_ShapeData == NULL) {
                    ephem_fatal(__FILE__, __LINE__, "Malloc fail.");
                    exit(1);
                }
            } else if (state == 1070) {
                // Entering group 1070, which defines the actual ephemeris data
                // Before we start we need to allocate storage for the data

                // Transfer the value of the variable "AU" from the quantities defined in GROUP 1041, to <JPL_AU>
                double *dptr;
                dictLookup(JPL_EphemVars, "AU", NULL, (void *) &dptr); // astronomical unit, measured in km
                JPL_AU = *dptr;
                if (DEBUG) {
                    snprintf(temp_err_string, FNAME_LENGTH, "Astronomical unit = %.1f km.", JPL_AU);
                    ephem_log(temp_err_string);
                }

                // Work out the number of records that the ephemeris will contain
                JPL_EphemArrayRecords = (int) ceil((JPL_EphemEnd - JPL_EphemStart) / JPL_EphemStep);

                // Work out how many bytes of storage we need
                malloced_data_len = JPL_EphemArrayLen * JPL_EphemArrayRecords;

                // Allocate storage for the ephemeris data
                JPL_EphemData = (double *) malloc(malloced_data_len * sizeof(double));
                if (JPL_EphemData == NULL) {
                    ephem_fatal(__FILE__, __LINE__, "Malloc fail.");
                    exit(1);
                }
            }
            continue;
        }

        if (state == 1010) {
            // Group 1010 defines the name of the ephemeris, and the start and end dates. We have already hard-coded these
            continue;

        } else if (state == 1030) {
            // Group 1030 has three numbers: the start and end Julian day numbers, and the step size (32 days)
            JPL_EphemStart = get_float(line, NULL);
            lineptr = next_word(line);
            JPL_EphemEnd = get_float(lineptr, NULL);
            lineptr = next_word(lineptr);
            JPL_EphemStep = get_float(lineptr, NULL);
            if (DEBUG) {
                snprintf(temp_err_string, FNAME_LENGTH, "Ephemeris spans from %.1f to %.1f; stepsize %.1f.",
                         JPL_EphemStart, JPL_EphemEnd, JPL_EphemStep);
                ephem_log(temp_err_string);
            }
        } else if (state == 1040) {
            // Group 1040 has a list of variables which are set within the header

            // The first line of GROUP 1040 is the number of items
            if (first) {
                first = 0;
                var_dict_len = (int) get_float(line, NULL);
                var_val = (double *) lt_malloc(var_dict_len * sizeof(double));
                if (var_val == NULL) {
                    ephem_fatal(__FILE__, __LINE__, "Malloc fail.");
                    exit(1);
                }
                if (DEBUG) {
                    sprintf(temp_err_string, "Variable dictionary has %d entries.", var_dict_len);
                    ephem_log(temp_err_string);
                }
                continue;
            }

            // Subsequent lines list the names of the variables in turn
            lineptr = line;
            while (lineptr[0] != '\0') {
                if (pos >= var_dict_len) {
                    ephem_fatal(__FILE__, __LINE__, "Variable dictionary overflow.");
                    exit(1);
                }
                for (i = 0; lineptr[i] > ' '; i++) key[i] = lineptr[i];
                key[i] = '\0';
                dictAppendPtr(JPL_EphemVars, key, var_val + (pos++), 0, 0, DATATYPE_VOID);
                lineptr = next_word(lineptr);
            }
        } else if (state == 1041) {
            // Group 1041 has a list of the values of the variables which are set within the header

            // The first line of GROUP 1041 is the number of items. This should match GROUP 1040's length!
            if (first) {
                double tmp = (int) get_float(line, NULL);
                first = 0;
                if (tmp != var_dict_len) {
                    ephem_fatal(__FILE__, __LINE__, "Inconsistent lengths stated for variable dictionary.");
                    exit(1);
                }
                continue;
            }

            // Each subsequent line defines the whitespace separated values of some variables
            lineptr = line;
            while (lineptr[0] != '\0') {
                if (pos >= var_dict_len) {
                    ephem_fatal(__FILE__, __LINE__, "Variable dictionary overflow.");
                    exit(1);
                }
                var_val[pos++] = (int) get_float(lineptr, NULL);
                lineptr = next_word(lineptr);
            }
        } else if (state == 1050) {
            // GROUP 1050 defines a 13x3 shape array, which we store in <JPL_ShapeData>
            lineptr = line;
            for (i = 0; lineptr[0] != '\0'; i++) {
                if (i >= 13) {
                    ephem_fatal(__FILE__, __LINE__, "Shape array horizontal overflow.");
                    exit(1);
                }
                if (pos >= 3) {
                    ephem_fatal(__FILE__, __LINE__, "Shape array vertical overflow.");
                    exit(1);
                }
                JPL_ShapeData[pos + 3 * i] = (int) get_float(lineptr, NULL);
                lineptr = next_word(lineptr);
            }
            pos++;
        } else if (state == 1070) {
            // GROUP 1070 is the actual ephemeris data itself

            // Ignore the line " 1 1018 " at the start of each block, which gives the length of each record
            if (strlen(line) < 40) continue;

            // The following two items within each block are jd_min and jd_max

            // Loop over the words on each line of the ephemeris
            lineptr = line;
            while (lineptr[0] != '\0') {
                if (count == 2) {
                    // Once we have read two items from the ephemeris, we have the min and max JD for the first block
                    // Set the variable jd_min for the current block
                    jd_min = JPL_EphemData[pos - 1];
                } else if ((count % JPL_EphemArrayLen) == 0) {
                    // We may ignore a block if it repeats data we already have
                    // ... but when the next block starts, we stop ignoring the data going past
                    ignore = 0;
                } else if ((count % JPL_EphemArrayLen) == 2) {
                    // We have just started a new block. Check that jd_min has the value we expect
                    if (JPL_EphemData[pos - 2] < jd_min - 0.1) {
                        // If we have a repeat block for a time span we've already passed, ignore the data
                        ignore = 1;
                    }

                    // Logging message if we have decided to ignore a block of data
                    if (DEBUG) {
                        if (ignore) {
                            snprintf(temp_err_string, FNAME_LENGTH, "Repeat record detected at %.1f (expecting %.1f).",
                                     JPL_EphemData[pos - 2], jd_min);
                            ephem_log(temp_err_string);
                        }
                    }

                    if (!ignore) {
                        // Update jd_min to reflect the new block we've just started reading
                        jd_min = JPL_EphemData[pos - 1];
                    } else {
                        // Throw out the jd_min and jd_max values for the block we started reading and are now ignoring
                        pos -= 2;
                    }
                }

                // Make sure that we've not read more data than the ephemeris header told us to allocate space for
                if (pos >= malloced_data_len) {
                    ephem_fatal(__FILE__, __LINE__, "Data array overflow.");
                    exit(1);
                }

                // Read the floating point numbers from the text file into a big array
                if (!ignore) JPL_EphemData[pos++] = get_float(lineptr, NULL);

                // if (DEBUG) {
                //  if ((pos % JPL_EphemArrayLen) == 2) {
                //   snprintf(temp_err_string, FNAME_LENGTH "Record spans from %.1f to %.1f.",
                //            JPL_EphemData[pos-2], JPL_EphemData[pos-1]);
                //   ephem_log(temp_err_string);
                //   }
                //  }

                // Proceed to the next word
                lineptr = next_word(lineptr);

                // Count the number of words we have read
                count++;
            }
        }
    }

    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "Read %d items into a data array of length %d.", pos,
                 malloced_data_len);
        ephem_log(temp_err_string);
    }
    if (DEBUG) {
        snprintf(temp_err_string, FNAME_LENGTH, "Finished reading JPL epemeris DE%d.", JPL_EphemNumber);
        ephem_log(temp_err_string);
    }

    // Now that we've parsed the text-based DE405 files that we downloaded, we dump the data in binary format
    JPL_DumpBinaryData();

    // Make table indicating that we have loaded all of the ephemeris data
    JPL_EphemData_items_loaded = (unsigned char *) lt_malloc(JPL_EphemArrayRecords * sizeof(unsigned char));
    memset(JPL_EphemData_items_loaded, 1, JPL_EphemArrayRecords);

    // Free storage for local copy
    free(JPL_EphemData);

    // Open file pointer to version on disk
    JPL_ReadBinaryData();
}

//! chebyshev - Evaluate a Chebyshev polynomial
//! \param coeffs - The coefficients of the Chebyshev polynomial
//! \param Ncoeff - The number of coefficients
//! \param x - The point at which to evaluate the Chebyshev polynomial
//! \return The value of the Chebyshev polynomial

double chebyshev(double *coeffs, int Ncoeff, double x) {
    double x2 = 2 * x;
    double d = 0, dd = 0, ddd = 0;
    int k = Ncoeff - 1;

    while (k > 0) {
        ddd = dd;
        dd = d;
        d = x2 * dd - ddd + coeffs[k];
        k--;
    }
    return x * d - dd + coeffs[0];
}

//! jpl_computeXYZ - Evaluate the 3D position of a solar system body at Julian date JD (in ICRF as used by DE405)
//! \param [in] body_id - The body's index within DE405 (0 Sun - 12 Pluto)
//! \param [in] jd - Julian day number; TT
//! \param [out] x - Cartesian position of body (AU). This axis points away from RA=0.
//! \param [out] y - Cartesian position of body (AU).
//! \param [out] z - Cartesian position of body (AU). This axis points towards J2000.0 north celestial pole

void jpl_computeXYZ(int body_id, double jd, double *x, double *y, double *z) {
    int record_index, i;
    double dt, tc;

#pragma omp critical (jpl_init)
    {
        // If we haven't already loaded DE405 data, make sure we have done so now
        if (JPL_EphemFile == NULL) jpl_readAsciiData();
    }

    // If this query falls outside the time span of DE405, then reject the query
    if ((JPL_EphemFile == NULL) || (jd < JPL_EphemStart) || (jd > JPL_EphemEnd)) {
        *x = *y = *z = GSL_NAN;
        return;
    }

    // Work out which block within DE405 this query falls within
    record_index = floor((jd - JPL_EphemStart) / JPL_EphemStep);

    // Clip block number within allowed range
    if (record_index < 0) record_index = 0;
    if (record_index >= JPL_EphemArrayRecords) record_index = JPL_EphemArrayRecords - 1;

    if (!JPL_EphemData_items_loaded[record_index]) {
        // Create a pointer to the block that we need to query
        long data_position_needed = JPL_EphemData_offset + record_index * JPL_EphemArrayLen * sizeof(double);

#pragma omp critical (jpl_fetch)
        {
            fseek(JPL_EphemFile, data_position_needed, SEEK_SET);
            dcffread((void *) &JPL_EphemData[record_index * JPL_EphemArrayLen],
                     sizeof(double), JPL_EphemArrayLen, JPL_EphemFile);
            JPL_EphemData_items_loaded[record_index] = 1;
        }
    }
    double *data = &JPL_EphemData[record_index * JPL_EphemArrayLen];

    double t0 = data[0]; // First JD of time step
    //double t1 = data[1]; // Last JD of time step

    // Read the shape array data about this body, which gives us 3 numbers...

    // Offset of start of Chebyshev coefficient list (FORTRAN numbering starts at 1!)
    int c = JPL_ShapeData[body_id * 3 + 0];

    // Number of Chebyshev coefficients
    int n = JPL_ShapeData[body_id * 3 + 1];

    // Number of sub-steps within time step
    int g = JPL_ShapeData[body_id * 3 + 2];

    if (g == 1) {
        // If the time step is not subdivided, then life is very easy...
        dt = JPL_EphemStep;  // size of whole time step
        tc = 2 * (jd - t0) / dt - 1; // time position within this step, scaled to range -1 to 1.
    } else {
        // Work out which subdivision we fall within...
        dt = JPL_EphemStep / g;  // size of each subdivision

        // Work out which subdivision we fall into, and clamp it within sensible range
        i = (int) floor((jd - t0) / dt);
        if (i >= g) i = g - 1;
        if (i < 0) i = 0;

        // Update the offset of start of Chebyshev coefficient list for this subdivision
        c += i * 3 * n;

        // time position within this step, scaled to range -1 to 1.
        tc = 2 * ((jd - t0) - i * dt) / dt - 1;
        if (tc < -1) tc = -1;
        if (tc > 1) tc = 1;
    }

    // Offset within block of coefficients uses FORTRAN numbering
    double *data_scan = data + (c - 1);

    // Evaluate the Chebyshev polynomial
    *x = chebyshev(data_scan, n, tc) / JPL_AU;
    *y = chebyshev(data_scan + 1 * n, n, tc) / JPL_AU;
    *z = chebyshev(data_scan + 2 * n, n, tc) / JPL_AU;

    // For diagnostics, it may be useful to print internal state
    // if (DEBUG) {
    //   snprintf(temp_err_string, FNAME_LENGTH,
    //            "t0 = %.1f ; t1 = %.1f ; JD = %.1f ; C = %d ; N = %d ; G = %d ; dt = %.1f ; Tc = %.1f ; "
    //            "x = %.1f ; y = %.1f ; z = %.1f", t0,t1,JD,C,N,G,dt,Tc,*x,*y,*z);
    //   ephem_log(temp_err_string);
    // }
}

//! jpl_computeEphemeris - Main entry point for estimating the position, brightness, etc of an object at a particular
//! time, using data from the DE405 ephemeris.
//! \param [in] i - Global settings used by ephemerisCompute
//! \param [in] bodyId - The object ID number we want to query. 0=Mercury. 2=Earth/Moon barycentre. 9=Pluto. 10=Sun, etc
//! \param [in] jd - The Julian date to query; TT
//! \param [out] x - x,y,z position of body, in ICRF v2, in AU, relative to solar system barycentre.
//! \param [out] y - x points to RA=0. y points to RA=6h.
//! \param [out] z - z points to celestial north pole (i.e. J2000.0).
//! \param [out] ra - Right ascension of the object (J2000.0, radians, relative to geocentre)
//! \param [out] dec - Declination of the object (J2000.0, radians, relative to geocentre)
//! \param [out] mag - Estimated V-band magnitude of the object
//! \param [out] phase - Phase of the object (0-1)
//! \param [out] angSize - Angular size of the object (arcseconds)
//! \param [out] phySize - Physical size of the object (metres)
//! \param [out] albedo - Albedo of the object
//! \param [out] sunDist - Distance of the object from the Sun (AU)
//! \param [out] earthDist - Distance of the object from the Earth (AU)
//! \param [out] sunAngDist - Angular distance of the object from the Sun, as seen from the Earth (radians)
//! \param [out] theta_ESO - Angular distance of the object from the Earth, as seen from the Sun (radians)
//! \param [out] eclipticLongitude - The ecliptic longitude of the object (J2000.0 radians)
//! \param [out] eclipticLatitude - The ecliptic latitude of the object (J2000.0 radians)
//! \param [out] eclipticDistance - The separation of the object from the Sun, in ecliptic longitude (radians)

void jpl_computeEphemeris(settings *i, int bodyId, double jd, double *x, double *y, double *z, double *ra, double *dec,
                          double *mag, double *phase, double *angSize, double *phySize, double *albedo, double *sunDist,
                          double *earthDist, double *sunAngDist, double *theta_ESO, double *eclipticLongitude,
                          double *eclipticLatitude, double *eclipticDistance) {
    double sun_pos_x, sun_pos_y, sun_pos_z;
    double EMX, EMY, EMZ; // Position of the Earth-Moon centre of mass
    double moon_pos_x, moon_pos_y, moon_pos_z;
    double earth_pos_x, earth_pos_y, earth_pos_z;

    // Boolean flags indicating whether we're dealing with a particular object which needs special treatment
    int is_moon = 0, is_earth = 0, is_sun = 0;

    // Body 19 is the Earth.
    // DE405 gives us the Earth/Moon barycentre (body 2), from which we subtract a small fraction of the Moon's
    // offset (body 9) to get the Earth's centre of mass
    if (bodyId == 19) {
        bodyId = 2;
        is_earth = 1;
    }

    // Body 9 is the Moon.
    // DE405 gives us a position relative to the Earth/Moon barycentre (body 2), so we add body 2's position
    if (bodyId == 9) {
        is_moon = 1;
    }

    // Body 10 is the Sun
    if (bodyId == 10) {
        is_sun = 1;
    }

    // We give asteroids body numbers which start at 1e6 + 1 (Ceres). These aren't in DE405, so use orbital elements.
    if (bodyId > 1000000) {
        orbitalElements_computeEphemeris(i, bodyId, jd, x, y, z, ra, dec, mag, phase, angSize, phySize, albedo, sunDist,
                                         earthDist, sunAngDist, theta_ESO, eclipticLongitude, eclipticLatitude,
                                         eclipticDistance);
        return;
    }

    // If we've got a query for a body which isn't in DE405, then we can't proceed
    if ((bodyId < 0) || (bodyId > 10)) {
        *x = *y = *z = *ra = *dec = GSL_NAN;
        return;
    }

    // DE405 gives us XYZ coordinates relative to the solar system's centre of mass
    // We need to know the Earth's position, in order to convert this to RA and Dec
    // Below are values of GM3 and GMM from DE405. See
    // <https://web.archive.org/web/20120220062549/http://iau-comm4.jpl.nasa.gov/de405iom/de405iom.pdf>
    const double earth_mass = 0.8887692390113509e-9;
    const double moon_mass = 0.1093189565989898e-10;
    const double moon_earth_mass_ratio = moon_mass / (moon_mass + earth_mass);

    // Look up the Sun's position
    jpl_computeXYZ(10, jd, &sun_pos_x, &sun_pos_y, &sun_pos_z);

    // Look up the Earth-Moon centre of mass position
    jpl_computeXYZ(2, jd, &EMX, &EMY, &EMZ);

    // Look up the Moon's position relative to the E-M centre of mass
    jpl_computeXYZ(9, jd, &moon_pos_x, &moon_pos_y, &moon_pos_z);

    // Calculate the position of the Earth's centre of mass
    earth_pos_x = EMX - moon_earth_mass_ratio * moon_pos_x;
    earth_pos_y = EMY - moon_earth_mass_ratio * moon_pos_y;
    earth_pos_z = EMZ - moon_earth_mass_ratio * moon_pos_z;

    if (is_earth) {
        // If the user's query was about the Earth, we already know its position
        *x = earth_pos_x;
        *y = earth_pos_y;
        *z = earth_pos_z;
    }
    else if (is_sun) {
        // If the user's query was about the Sun, we already know that position too!
        *x = sun_pos_x;
        *y = sun_pos_y;
        *z = sun_pos_z;
    }
    else if (is_moon) {
        // If the user's query was about the Moon, we already know that position too!
        *x = moon_pos_x;
        *y = moon_pos_y;
        *z = moon_pos_z;
    }
    else {
        // otherwise we need to query DE405 for the particular object the user was looking for
        jpl_computeXYZ(bodyId, jd, x, y, z);
    }

    // If the body was the Moon, don't forget the add the position of the Earth-Moon centre of mass
    if (is_moon) {
        *x += earth_pos_x;
        *y += earth_pos_y;
        *z += earth_pos_z;
    }

    // Populate other quantities, like the brightness, RA and Dec of the object, based on its XYZ position
    magnitudeEstimate(bodyId, *x, *y, *z, earth_pos_x, earth_pos_y, earth_pos_z, sun_pos_x, sun_pos_y, sun_pos_z, ra,
                      dec, mag, phase, angSize, phySize,
                      albedo, sunDist, earthDist, sunAngDist, theta_ESO, eclipticLongitude, eclipticLatitude,
                      eclipticDistance, i);
}


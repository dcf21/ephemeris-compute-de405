// orbitalElements.h
// 
// -------------------------------------------------
// Copyright 2015-2022 Dominic Ford
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

#ifndef ORBITALELEMENTS_H
#define ORBITALELEMENTS_H 1

#include "settings/settings.h"

#define MAX_ASTEROIDS 1500000
#define MAX_COMETS     100000
#define MAX_PLANETS        50

typedef struct {
    char name[24], name2[24];
    int number;  // bodyId for planets; bodyId-1000000 for asteroids; bodyId-2000000 for comets
    int secureOrbit;  // boolean flag indicating whether orbit is deemed secure
    double epochOsculation;  // Julian date
    double epochPerihelion;  // Julian date
    double absoluteMag;  // Absolute magnitude H
    double meanAnomaly;  // mean anomaly at epoch of osculation; radians
    double argumentPerihelion;  // argument of perihelion at epoch of osculation; radians; J2000.0
    double argumentPerihelion_dot;  // rate of change; radians per day
    double longAscNode;  // longitude of ascending node; radians; J2000.0
    double longAscNode_dot;  // rate of change; radians per day
    double inclination;  // radians; J2000.0
    double inclination_dot;  // rate of change; radians per day
    double eccentricity;
    double eccentricity_dot; // rate of change; per day
    double semiMajorAxis;  // AU
    double semiMajorAxis_dot; // rate of change; AU per day
    double slopeParam_n, slopeParam_G;
} orbitalElements;

#ifndef ORBITALELEMENTS_C
// Binary files containing the orbital elements of solar system objects
extern FILE *planet_database_file;
extern FILE *asteroid_database_file;
extern FILE *comet_database_file;

// Blocks of memory used to hold the orbital elements
extern orbitalElements *planet_database;
extern orbitalElements *asteroid_database;
extern orbitalElements *comet_database;

// Record of which entries we have loaded from each database
extern unsigned char *planet_database_items_loaded;
extern unsigned char *asteroid_database_items_loaded;
extern unsigned char *comet_database_items_loaded;

// Pointers to the positions in the files where the orbitalElement structures begin
extern int planet_database_offset;
extern int asteroid_database_offset;
extern int comet_database_offset;

// Number of objects in each list
extern int planet_count;
extern int asteroid_count;
extern int comet_count;

// Number of objects with securely determined orbits
extern int planet_secure_count;
extern int asteroid_secure_count;
extern int comet_secure_count;
#endif

void orbitalElements_planets_init();

orbitalElements *orbitalElements_planets_fetch(int index);

void orbitalElements_asteroids_init();

orbitalElements *orbitalElements_asteroids_fetch(int index);

void orbitalElements_comets_init();

orbitalElements *orbitalElements_comets_fetch(int index);

void orbitalElements_computeXYZ(int body_id, double jd, double *x, double *y, double *z);

void orbitalElements_computeEphemeris(settings *i, int bodyId, double jd, double *x, double *y, double *z, double *ra,
                                      double *dec, double *mag, double *phase, double *angSize, double *phySize,
                                      double *albedo, double *sunDist, double *earthDist, double *sunAngDist,
                                      double *theta_eso, double *eclipticLongitude, double *eclipticLatitude,
                                      double *eclipticDistance);

#endif

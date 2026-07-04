/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Betaflight. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef USE_FLIGHT_PLAN

#define MAX_MISSION_WAYPOINTS 15

typedef enum {
    WP_TYPE_FLYOVER    = 0,
    WP_TYPE_FLYBY      = 1,
    WP_TYPE_HOLD       = 2,
    WP_TYPE_LAND       = 3,
    WP_TYPE_TAKEOFF    = 4,
    WP_TYPE_ALT_CHANGE = 5,
    WP_TYPE_DELAY      = 6,
    WP_TYPE_YAW_RATE   = 7,
} missionWpType_e;

typedef enum {
    WP_PATTERN_ORBIT    = 0,
    WP_PATTERN_FIGURE8  = 1,
} missionWpPattern_e;

typedef struct {
    int32_t  latitude;      // 1e-7 deg
    int32_t  longitude;     // 1e-7 deg
    float    altitude;      // cm
    float    speed;         // cm/s (YAW_RATE 타입일 때는 deg/s)
    missionWpType_e     type;
    float               duration;      // deciseconds
    missionWpPattern_e  pattern;
} missionWaypoint_t;

extern missionWaypoint_t missionWaypoints[MAX_MISSION_WAYPOINTS];
extern uint8_t missionWpCount;
extern uint8_t currentMissionWpIndex;

void missionInit(void);
void missionStart(void);
void missionStop(void);
bool missionIsActive(void);

void missionUpdateTargetOnly(void);
bool missionCheckAdvance(void);

void missionClear(void);
bool missionInsert(int idx, const missionWaypoint_t *wp);
bool missionRemove(int idx);

const char *wpTypeToStr(missionWpType_e type);
missionWpType_e strToWpType(const char *str);
const char *wpPatternToStr(missionWpPattern_e pattern);
missionWpPattern_e strToWpPattern(const char *str);

#endif // USE_FLIGHT_PLAN

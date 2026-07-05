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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "platform.h"

#ifdef USE_FLIGHT_PLAN

#include "build/debug.h"

#include "common/utils.h"
#include "common/maths.h"

#include "drivers/time.h"

#include "io/gps.h"

#include "fc/runtime_config.h"

#include "pg/gps_rescue.h"

#include "flight/gps_rescue.h"
#include "mission.h"

#include "pg/mission.h"

/* ================================================================
 * 미션 저장소 (PG-backed RAM copy)
 * ================================================================ */

missionWaypoint_t missionWaypoints[MAX_MISSION_WAYPOINTS];
uint8_t missionWpCount = 0;
uint8_t currentMissionWpIndex = 0;

static bool isMissionActive = false;

// 미션 타임아웃 5분
#define MISSION_WP_TIMEOUT_US 300000000

static timeUs_t wpEntryTime = 0;
static float prevDistCm = -1.0f;
static bool wasClosing = false;

/* ================================================================
 * 내부 헬퍼
 * ================================================================ */

static void missionApplyWaypoint(void)
{
    if (currentMissionWpIndex >= missionWpCount) {
        missionStop();
        return;
    }
}

/* ================================================================
 * 미션 제어 함수
 * ================================================================ */

void missionStart(void)
{
    // Rescue 재진입 허용: 하강/착륙/비상 단계가 아니면 Autopilot 재시작 가능
    const rescuePhase_e phase = gpsRescueGetPhase();
    if (phase == RESCUE_DESCENT ||
        phase == RESCUE_LANDING ||
        phase == RESCUE_SHUTTLE_DESCENT ||
        phase == RESCUE_ABORT ||
        phase == RESCUE_DO_NOTHING ||
        phase == RESCUE_COMPLETE) {
        return;
    }
    if (missionWpCount == 0) {
        // Waypoint 없음 → Rescue 실행
        isMissionActive = false;
        rescueState.phase = RESCUE_INITIALIZE;
        return;
    }
    currentMissionWpIndex = 0;
    isMissionActive = true;
    wpEntryTime = micros();
    prevDistCm = -1.0f;
    wasClosing = false;
    missionApplyWaypoint();
}

void missionStop(void)
{
    isMissionActive = false;
    wpEntryTime = 0;
    prevDistCm = -1.0f;
    wasClosing = false;

    // Home Fix 유무에 따라 분기
    if (STATE(GPS_FIX_HOME)) {
        // Home point 있음 → Home 귀환
        // RESCUE_INITIALIZE 수준의 상태 초기화 (CPA/셔틀/고도 래치 리셋)
        gpsRescueResetState();

        rescueState.phase = RESCUE_FLY_HOME;
        currentVCLat = GPS_home[0];
        currentVCLon = GPS_home[1];
    } else {
        // Home point 없음 → 현재 위치 기준 헤딩 기반 무한셔틀
        gpsRescueStartShuttleInfinite();
    }
}

bool missionIsActive(void)
{
    return isMissionActive && (currentMissionWpIndex < missionWpCount);
}

/* ================================================================
 * Mission 타겟 설정
 * ================================================================ */

void missionUpdateTargetOnly(void)
{
    rescuePhase_e phase = gpsRescueGetPhase();
    if (phase == RESCUE_ABORT ||
        phase == RESCUE_DO_NOTHING ||
        phase == RESCUE_LANDING) {
        missionStop();
        return;
    }
    if (!missionIsActive()) {
        return;
    }

    missionWaypoint_t *wp = &missionWaypoints[currentMissionWpIndex];
    currentVCLat = wp->latitude;
    currentVCLon = wp->longitude;
    rescueState.intent.targetAltitudeCm = wp->altitude;

    // 목표 속도: waypoint 속도와 Rescue 최소 속도 중 큰 값
    rescueState.intent.targetVelocityCmS = MAX(
        wp->speed,
        (float)gpsRescueConfig()->groundSpeedCmS
    );

    GPS_distance_cm_bearing(&gpsSol.llh.lat, &gpsSol.llh.lon,
                           &currentVCLat, &currentVCLon,
                           &rescueState.intent.distanceToTargetCm,
                           &rescueState.intent.directionToTargetCd);
}

/* ================================================================
 * Mission 진행 체크 및 WP 전환
 * ================================================================ */

bool missionCheckAdvance(void)
{
    if (!missionIsActive()) {
        return false;
    }

    // 타임아웃 — 5분 초과 시 강제 Skip
    if (cmpTimeUs(micros(), wpEntryTime) > MISSION_WP_TIMEOUT_US) {
        currentMissionWpIndex++;
        if (currentMissionWpIndex >= missionWpCount) {
            missionStop();
        } else {
            wpEntryTime = micros();
            prevDistCm = -1.0f;
            wasClosing = false;
            missionApplyWaypoint();
        }
        return true;
    }

    // CPA (Closest Point of Approach) 도착 체크 — 계획서 v2 wasClosing 로직
    const float dCm = (float)rescueState.intent.distanceToTargetCm;

    if (dCm < GPS_RESCUE_TOUCH_ACTIVATION_CM && dCm >= 0) {
        if (prevDistCm < 0) {
            prevDistCm = dCm;
            wasClosing = true;
            return false;
        }

        bool isClosing = (dCm < prevDistCm - 20.0f);
        bool touchCPA = false;
        if (!isClosing && wasClosing) {
            touchCPA = true;   // 멀어지기 시작했으므로 CPA 도달
        }
        wasClosing = isClosing;
        prevDistCm = dCm;

        if (touchCPA || dCm < GPS_RESCUE_TOUCH_PROXIMITY_CM) {
            // WP 전환
            currentMissionWpIndex++;
            if (currentMissionWpIndex >= missionWpCount) {
                missionStop();
            } else {
                wpEntryTime = micros();
                prevDistCm = -1.0f;
                wasClosing = false;
                missionApplyWaypoint();
            }
            return true;
        }
    } else {
        // 범위 밖에서는 prevDistCm 리셋 (다시 진입 시 신선한 측정)
        prevDistCm = -1.0f;
    }

    return false;
}

/* ================================================================
 * 데이터 접근 함수
 * ================================================================ */

void missionInit(void)
{
    // PG에서 waypoint 복원 (save/reboot 시 유지됨)
    const missionConfig_t *cfg = missionConfig();
    missionWpCount = cfg->waypointCount;
    for (int i = 0; i < missionWpCount; i++) {
        missionWaypoints[i] = cfg->waypoints[i];
    }
    currentMissionWpIndex = 0;
    isMissionActive = false;
    wpEntryTime = 0;
    prevDistCm = -1.0f;
    wasClosing = false;
}

void missionClear(void)
{
    memset(missionWaypoints, 0, sizeof(missionWaypoints));
    missionWpCount = 0;

    // PG에도 반영
    missionConfigMutable()->waypointCount = 0;
    memset(missionConfigMutable()->waypoints, 0, sizeof(missionConfigMutable()->waypoints));
}

bool missionInsert(int idx, const missionWaypoint_t *wp)
{
    if (idx < 0 || idx > missionWpCount || missionWpCount >= MAX_MISSION_WAYPOINTS) {
        return false;
    }

    // 삽입 위치 이후 Waypoint를 한 칸씩 뒤로 이동
    for (int i = missionWpCount; i > idx; i--) {
        memcpy(&missionWaypoints[i], &missionWaypoints[i - 1], sizeof(missionWaypoint_t));
    }

    memcpy(&missionWaypoints[idx], wp, sizeof(missionWaypoint_t));
    missionWpCount++;

    // PG에도 반영
    missionConfigMutable()->waypointCount = missionWpCount;
    for (int i = 0; i < missionWpCount; i++) {
        missionConfigMutable()->waypoints[i] = missionWaypoints[i];
    }

    return true;
}

bool missionRemove(int idx)
{
    if (idx < 0 || idx >= missionWpCount) {
        return false;
    }

    // 삭제 위치 이후 Waypoint를 한 칸씩 앞으로 이동
    for (int i = idx; i < missionWpCount - 1; i++) {
        memcpy(&missionWaypoints[i], &missionWaypoints[i + 1], sizeof(missionWaypoint_t));
    }
    missionWpCount--;

    // PG에도 반영
    missionConfigMutable()->waypointCount = missionWpCount;
    for (int i = 0; i < missionWpCount; i++) {
        missionConfigMutable()->waypoints[i] = missionWaypoints[i];
    }

    return true;
}

/* ================================================================
 * 문자열 ↔ enum 변환
 * ================================================================ */

static const char * const wpTypeNames[] = {
    [WP_TYPE_FLYOVER]    = "FLYOVER",
    [WP_TYPE_FLYBY]      = "FLYBY",
    [WP_TYPE_HOLD]       = "HOLD",
    [WP_TYPE_LAND]       = "LAND",
    [WP_TYPE_TAKEOFF]    = "TAKEOFF",
    [WP_TYPE_ALT_CHANGE] = "ALT_CHANGE",
    [WP_TYPE_DELAY]      = "DELAY",
    [WP_TYPE_YAW_RATE]   = "YAW_RATE",
};

const char *wpTypeToStr(missionWpType_e type)
{
    if (type > WP_TYPE_YAW_RATE) {
        return "FLYOVER";
    }
    return wpTypeNames[type];
}

missionWpType_e strToWpType(const char *str)
{
    if (strcasecmp(str, "FLYBY") == 0)      return WP_TYPE_FLYBY;
    if (strcasecmp(str, "HOLD") == 0)        return WP_TYPE_HOLD;
    if (strcasecmp(str, "LAND") == 0)        return WP_TYPE_LAND;
    if (strcasecmp(str, "TAKEOFF") == 0)     return WP_TYPE_TAKEOFF;
    if (strcasecmp(str, "ALT_CHANGE") == 0)  return WP_TYPE_ALT_CHANGE;
    if (strcasecmp(str, "DELAY") == 0)       return WP_TYPE_DELAY;
    if (strcasecmp(str, "YAW_RATE") == 0)    return WP_TYPE_YAW_RATE;
    return WP_TYPE_FLYOVER;  // default
}

static const char * const wpPatternNames[] = {
    [WP_PATTERN_ORBIT]    = "ORBIT",
    [WP_PATTERN_FIGURE8]  = "FIGURE8",
};

const char *wpPatternToStr(missionWpPattern_e pattern)
{
    if (pattern > WP_PATTERN_FIGURE8) {
        return "ORBIT";
    }
    return wpPatternNames[pattern];
}

missionWpPattern_e strToWpPattern(const char *str)
{
    if (strcasecmp(str, "FIGURE8") == 0) return WP_PATTERN_FIGURE8;
    return WP_PATTERN_ORBIT;  // default
}

#endif // USE_FLIGHT_PLAN

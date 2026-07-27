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
static bool wp1TooFar = false;  // 첫 WP가 홈에서 500m 이상 떨어져 있음 (OSD 경고용)
static bool missionCompletedFlag = false;  // 미션 완료 후 재시작 방지 (무한루프)

static timeUs_t wpEntryTime = 0;
static float prevDistCm = -1.0f;
static bool wasClosing = false;

// 글라이드 슬로프 보간 상태
#define WP_ALT_GLIDE_MIN_DISTANCE_CM 500.0f
static float wpGlideStartAltitudeCm = 0;
static float wpGlideStartDistanceCm = 0;
static bool wpGlideInitialized = false;

/* ================================================================
 * 전방 선언
 * ================================================================ */

static void missionApplyWaypoint(void);
void missionStopAndGoHome(void);

/* ================================================================
 * 내부 헬퍼
 * ================================================================ */

static void missionApplyWaypoint(void)
{
    if (currentMissionWpIndex >= missionWpCount) {
        missionStopAndGoHome();
        return;
    }
    wpGlideInitialized = false;  // 새 waypoint 진입 → 글라이드 슬로프 재초기화
    missionUpdateTargetOnly();  // 즉시 타겟 좌표/고도/속도 업데이트
}

/* ================================================================
 * 미션 제어 함수
 * ================================================================ */

void missionStart(void)
{
    // Rescue 재진입 허용: 하강/착륙/비상이 아니면 Autopilot 재시작 가능
    // (FLY_HOME/ATTAIN_ALT는 gps_rescue.c에서 "Autopilot 재진입" 목적으로 명시적으로
    //  missionStart()를 호출하는 phase이므로 여기서 차단하면 그 호출부가 죽은 코드가 됨)
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
        // Waypoint 없음 → phase 변경 없이 리턴 (gps_rescue.c에서 일반 Rescue 처리)
        isMissionActive = false;
        wp1TooFar = false;
        return;
    }

    // 🆕 미션 완료 후 재시작 방지: 이미 모든 WP를 소진한 상태면 early return
    // (missionStopAndGoHome()에 의해 missionCompletedFlag=true 설정됨)
    // 다른 모드(셔틀/레스큐)로 전환 후 재진입 시에는 missionStop()이 호출되므로
    // missionCompletedFlag가 유지되어 재시작이 차단됨 → missionClear() 또는
    // missionStart() 진입 시 리셋 필요
    if (missionCompletedFlag) {
        return;
    }

    // 🆕 첫 번째 Waypoint 홈 거리 검증 (안전장치)
    if (!missionValidateFirstWaypoint()) {
        // 첫 WP가 홈에서 500m 이상 떨어져 있음 → 미션 취소, 일반 Rescue로 Fallback
        isMissionActive = false;
        wp1TooFar = true;
        return;
    }
    wp1TooFar = false;

    currentMissionWpIndex = 0;
    isMissionActive = true;
    missionCompletedFlag = false;  // 새 미션 시작 시 플래그 리셋
    wpEntryTime = micros();
    prevDistCm = GPS_RESCUE_CPA_UNINITIALIZED;
    wasClosing = false;
    missionApplyWaypoint();
}

void missionStop(void)
{
    // 미션 상태만 초기화. phase는 변경하지 않음 (호출부에서 별도로 처리)
    isMissionActive = false;
    wp1TooFar = false;
    wpEntryTime = 0;
    prevDistCm = GPS_RESCUE_CPA_UNINITIALIZED;
    wasClosing = false;
    wpGlideInitialized = false;
    missionCompletedFlag = false;  // 🆕 다른 모드 전환 시 재진입 가능하도록 리셋
}

void missionStopAndGoHome(void)
{
    // 미션 중지 + 홈 귀환 (정상 완료 시 사용)
    missionStop();
    missionCompletedFlag = true;  // 🆕 모든 WP 완료 표시 (재시작 방지)

    if (STATE(GPS_FIX_HOME)) {
        gpsRescueResetState();

        rescueState.phase = RESCUE_FLY_HOME;
        currentVCLat = GPS_home[0];
        currentVCLon = GPS_home[1];
        rescueState.intent.targetAltitudeCm = rescueState.intent.returnAltitudeCm;  // 안전 귀환 고도 보장
    } else {
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
        phase == RESCUE_LANDING ||
        phase == RESCUE_DESCENT ||
        phase == RESCUE_SHUTTLE_DESCENT) {
        // mission만 중지하고 phase는 유지 (안전 개입 무효화 방지)
        isMissionActive = false;
        return;
    }
    if (!missionIsActive()) {
        return;
    }

    missionWaypoint_t *wp = &missionWaypoints[currentMissionWpIndex];
    currentVCLat = wp->latitude;
    currentVCLon = wp->longitude;

    // --- 글라이드 슬로프: 목표 고도 점진적 보간 ---
    // 웨이포인트가 처음 활성화되면 현재 위치/고도를 기준으로 글라이드 시작점 기록
    // 이후 매 루프 남은 수평 거리에 비례해 목표 고도를 선형 보간함
    const float currentDistanceCm = (float)rescueState.intent.distanceToTargetCm;

    if (!wpGlideInitialized || currentDistanceCm > wpGlideStartDistanceCm) {
        // 첫 진입 또는 바람 등으로 거리가 늘어난 경우: 현재 고도에서 다시 시작
        wpGlideStartAltitudeCm = (float)rescueState.intent.targetAltitudeCm;
        wpGlideStartDistanceCm = currentDistanceCm;
        wpGlideInitialized = true;
    }

    if (currentDistanceCm >= WP_ALT_GLIDE_MIN_DISTANCE_CM && wpGlideStartDistanceCm > 0) {
        // 거리 비례 보간: progress = 1.0 → waypoint 고도, progress = 0.0 → 시작 고도
        const float progress = constrainf(
            1.0f - (currentDistanceCm / wpGlideStartDistanceCm),
            0.0f,
            1.0f
        );
        rescueState.intent.targetAltitudeCm = (int32_t)(
            wpGlideStartAltitudeCm + (float)(wp->altitude - (int32_t)wpGlideStartAltitudeCm) * progress
        );
    } else {
        // 최소 거리 미만이거나 시작 거리가 0이면 즉시 목표 고도 사용 (폴백)
        rescueState.intent.targetAltitudeCm = wp->altitude;
    }

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

    const float dCm = (float)rescueState.intent.distanceToTargetCm;

    if (gpsRescueCPATouchCheck(dCm, GPS_RESCUE_TOUCH_ACTIVATION_CM,
                                             GPS_RESCUE_TOUCH_PROXIMITY_CM,
                                             &prevDistCm, &wasClosing)) {
        // WP 전환
        currentMissionWpIndex++;
        if (currentMissionWpIndex >= missionWpCount) {
            missionStopAndGoHome();
        } else {
            wpEntryTime = micros();
            missionApplyWaypoint();
        }
        return true;
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
    missionWpCount = MIN(cfg->waypointCount, MAX_MISSION_WAYPOINTS);
    for (int i = 0; i < missionWpCount; i++) {
        missionWaypoints[i] = cfg->waypoints[i];
    }
    currentMissionWpIndex = 0;
    isMissionActive = false;
    wp1TooFar = false;
    wpEntryTime = 0;
    prevDistCm = GPS_RESCUE_CPA_UNINITIALIZED;
    wasClosing = false;
    wpGlideInitialized = false;   // 재부팅/재초기화 시 글라이드 상태도 초기화
}

void missionClear(void)
{
    memset(missionWaypoints, 0, sizeof(missionWaypoints));
    missionWpCount = 0;
    currentMissionWpIndex = 0;
    isMissionActive = false;
    wp1TooFar = false;
    missionCompletedFlag = false;  // 🆕 WP 클리어 시 완료 플래그도 리셋

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

    // currentMissionWpIndex 보정 (삭제된 WP보다 뒤에 있었으면 인덱스 감소)
    if (currentMissionWpIndex > idx && currentMissionWpIndex > 0) {
        currentMissionWpIndex--;
    } else if (currentMissionWpIndex == idx && currentMissionWpIndex < missionWpCount) {
        // 현재 활성 WP가 삭제됨 → 새 WP가 같은 위치로 슬라이드됨
        // missionApplyWaypoint()로 타겟 좌표/고도 즉시 갱신하여 stale 좌표 비행 방지
        missionApplyWaypoint();
    }

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

/* ================================================================
 * 첫 번째 Waypoint 홈 거리 검증 (안전장치)
 * ================================================================ */

bool missionValidateFirstWaypoint(void)
{
    // Home 위치가 없으면 검증 불가 → 안전하게 미션 실행 안 함
    if (!STATE(GPS_FIX_HOME)) {
        return false;
    }

    if (missionWpCount == 0) {
        return false;
    }

    // 첫 번째 Waypoint 좌표
    missionWaypoint_t *wp = &missionWaypoints[0];

    // 홈 위치와 첫 WP 간 거리 계산
    uint32_t distCm;
    int32_t bearingCd;
    GPS_distance_cm_bearing(&GPS_home[0], &GPS_home[1],
                            &wp->latitude, &wp->longitude,
                            &distCm, &bearingCd);

    // 500m 이상이면 검증 실패
    if (distCm >= MISSION_FIRST_WP_MAX_DISTANCE_CM) {
        return false;
    }

    return true;
}

bool missionIsWp1TooFar(void)
{
    return wp1TooFar;
}

bool missionHasCompleted(void)
{
    return missionCompletedFlag;
}

#endif // USE_FLIGHT_PLAN

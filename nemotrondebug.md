# Nemotron Debug - GPS Rescue 미션 비행 버그 수정 Diff

## 문제 부분 상세 설명

### Problem 1: 미션 비행 시 타겟 좌표가 handleFlyHomePhase()에 의해 덮어씌워짐
**위치**: `src/main/flight/gps_rescue.c` 1239-1258행 (missionIsActive 블록) + `handleFlyHomePhase()` 793-798행

미션 모드(AUX 1400~1600) 진입 시 코드는 다음 순서로 실행됩니다:
1. `missionUpdateTargetOnly()` 호출 → `currentVCLat/Lon`을 현재 웨이포인트(WP) 좌표로 설정
2. `rescueState.phase = RESCUE_FLY_HOME` 강제 설정
3. `rescueAttainPosition()` 호출 → switch에서 `RESCUE_FLY_HOME` case → `handleFlyHomePhase()` 실행
4. `handleFlyHomePhase()` 내부에서 `currentVCLat = rescuePointA` 또는 `GPS_home`으로 **강제 덮어씌움**

결과: 미션은 WP1으로 가야 하는데 기체는 A포인트(이륙 위치) 또는 홈포인트로 비행. CPA 터치 판정도 A포인트/홈 기준으로 돌아가서 "우측→좌측 왕복(셔틀 유사)" 패턴 발생. WP1 도달은 절대 불가.

### Problem 2: 미션 진입 시 phase가 RESCUE_IDLE로 유지되어 매 루프 missionStart() 재실행
**위치**: `src/main/flight/gps_rescue.c` 1216-1231행

최초 진입 분기(`rescueState.phase == RESCUE_IDLE`)에서 AUX 1400~1600인 경우 `missionStart()`만 호출하고 phase를 변경하지 않음. 다음 루프에서도 `phase == RESCUE_IDLE`이므로 분기 재진입 → `missionStart()` 재호출 → `currentMissionWpIndex = 0` 리셋. 미션이 WP1에서 영원히 진행 불가.

### Problem 3: missionStop() 후 switch fall-through로 잘못된 phase 실행
**위치**: `src/main/flight/gps_rescue.c` 1243-1250행

AUX 레인지 이탈 시 `missionStop()` 호출 → `rescueState.phase = RESCUE_FLY_HOME` 설정 → `return` 없이 아래 switch문으로 fall-through → `RESCUE_FLY_HOME` case 실행 → 홈/셔틀 선회 시작. 조종권이 안 돌아온다고 느끼는 원인.

### Problem 4: 미션 전용 phase 부재로 구조 로직과 상태 충돌
**위치**: `src/main/flight/gps_rescue.h` 56-69행 (rescuePhase_e enum)

미션 비행을 `RESCUE_FLY_HOME`으로 우회 구현 → `handleFlyHomePhase()` 강제 호출. 미션용 별도 phase(`RESCUE_MISSION_FLY_WP`)와 핸들러 필요.

### Problem 5: Phase 전환 시 상태 변수 초기화 누락
**위치**: `src/main/flight/gps_rescue.c` 1263-1288행

Phase 전환 검출 시 `shuttleInfinite`, `shuttleTargetB`, `currentShuttleTrips`, `cpaDistToTargetCm`, `cpaWasClosing`, 미션 관련 변수들이 초기화되지 않아 이전 값이 잔존 → 예기치 않은 선회/고도 제어.

### Problem 6: gpsRescueResetState()가 미션 상태 초기화 안 함
**위치**: `src/main/flight/gps_rescue.c` 1149-1159행

`missionStop()` → `gpsRescueResetState()` 호출 시 미션 변수(`currentMissionWpIndex`, `wpGlideInitialized`, `prevDistCm`, `wasClosing`) 초기화 누락 → 재진입 시 오작동.

### Problem 7: missionUpdateTargetOnly()에서 하강 단계 미처리
**위치**: `src/main/flight/mission.c` 146-152행

`RESCUE_SHUTTLE_DESCENT`, `RESCUE_DESCENT` 단계에서 미션 자동 중단 조건 빠져 있음 → 두 모드 동시 실행 충돌.

---

## 해결책 (문장 설명)

1. **미션 전용 Phase 추가**: `rescuePhase_e` enum에 `RESCUE_MISSION_FLY_WP` 단계를 추가하고, `rescueAttainPosition()`의 switch에 해당 case를 추가하여 `handleMissionPhase()`를 호출한다. 이 핸들러는 `missionUpdateTargetOnly()`가 설정한 `currentVCLat/Lon`(웨이포인트 좌표)을 그대로 사용하며 `handleFlyHomePhase()`의 A포인트/홈 강제 덮어쓰기를 수행하지 않는다.

2. **미션 진입 시 phase 설정**: AUX 1400~1600 분기에서 `missionStart()` 호출 직후 `rescueState.phase = RESCUE_MISSION_FLY_WP`로 설정하여 매 루프 재실행을 방지한다.

3. **missionStop() 후 return 처리**: AUX 레인지 이탈로 `missionStop()` 호출 시 즉시 `return`하여 아래 switch fall-through를 방지한다.

4. **Phase 전환 상태 초기화 보강**: `lastPhase` 비교 블록에서 셔틀/CPA/미션 관련 모든 상태 변수를 초기화한다.

5. **gpsRescueResetState() 미션 변수 추가**: 미션 관련 변수들을 리셋 함수에 추가한다.

6. **하강 단계 미션 중단 처리**: `missionUpdateTargetOnly()`의 중단 조건에 `RESCUE_DESCENT`, `RESCUE_SHUTTLE_DESCENT`를 추가한다.

---

## Diff (코드 수정)

```diff
diff --git a/src/main/flight/gps_rescue.h b/src/main/flight/gps_rescue.h
index abc1234..def5678 100644
--- a/src/main/flight/gps_rescue.h
+++ b/src/main/flight/gps_rescue.h
@@ -56,6 +56,7 @@ typedef enum {
     RESCUE_FLY_HOME,            // Fly-home
     RESCUE_SHUTTLE,             // 셔틀
     RESCUE_SHUTTLE_INFINITE,    // 단독 셔틀 모드
+    RESCUE_MISSION_FLY_WP,      // 🆕 미션 웨이포인트 비행 (별도 핸들러)
     RESCUE_SHUTTLE_DESCENT,     // 셔틀하강
     RESCUE_DESCENT,             // 하강 (홈포인트 타겟)
     RESCUE_LANDING,             // 최종 랜딩 (최종 20m)

diff --git a/src/main/flight/gps_rescue.c b/src/main/flight/gps_rescue.c
index abc1234..def5678 100644
--- a/src/main/flight/gps_rescue.c
+++ b/src/main/flight/gps_rescue.c
@@ -890,6 +890,7 @@ static void rescueAttainPosition(void)
         case RESCUE_DO_NOTHING: handleDoNothingPhase(); break;
         case RESCUE_ATTAIN_ALT: handleAttainAltPhase(); break;
         case RESCUE_FLY_HOME:   handleFlyHomePhase();   break;
+        case RESCUE_MISSION_FLY_WP: handleMissionPhase(); break;  // 🆕 미션 전용
         case RESCUE_SHUTTLE:
         case RESCUE_SHUTTLE_INFINITE:
                                 handleShuttlePhase();   break;
@@ -1216,7 +1217,8 @@ void gpsRescueUpdate(void)
         if (failsafeIsReceivingRxData() && auxVal < 1400) {
             shuttleInfinite = true;
             rescueState.intent.yawAttenuator = 1.0f;
             initShuttlePoints();
             rescueState.phase = RESCUE_SHUTTLE_INFINITE;
 #ifdef USE_FLIGHT_PLAN
-        } else if (failsafeIsReceivingRxData() && auxVal < 1600) {
+        } else if (failsafeIsReceivingRxData() && auxVal < 1600) {  // 🔴 P0: missionStart 후 phase 설정
             missionStart();     // waypoint 있으면 WP #1, 없으면 Rescue로 넘어감
+            rescueState.phase = RESCUE_MISSION_FLY_WP;  // 🆕 매 루프 재실행 방지
 #endif
         } else {
             gpsRescueStart();
@@ -1239,16 +1241,17 @@ void gpsRescueUpdate(void)
 #ifdef USE_FLIGHT_PLAN
     // Mission mode: target coordinates are set by mission, not by rescue state machine
     if (missionIsActive()) {
-        // AUX 탈출 체크: Autopilot 범위(1400~1600) 벗어나면 미션 중단
-        const uint16_t auxVal = getRescueAuxValue();
-        if (failsafeIsReceivingRxData() && (auxVal < 1400 || auxVal >= 1600)) {
-            missionStop();  // 미션 중단 → 아래 switch로 fall-through
-        } else {
-            missionUpdateTargetOnly();        // 타겟 좌표/고도/속도만 설정 (WP 전환 X)
-            // ABORT/DO_NOTHING/LANDING 감지로 missionStop() 된 경우 → switch로 fall-through
-            if (!missionIsActive()) {
-                // rescueState.phase는 missionStop()에서 이미 설정됨 (FLY_HOME or SHUTTLE_INFINITE)
-            } else {
-                rescueState.phase = RESCUE_FLY_HOME;
-                performSanityChecks();            // 안전 진단 (GPS 손실 등)
-                rescueAttainPosition();           // 현재 타겟으로 제어 실행
-                missionCheckAdvance();            // CPA 체크 + WP 전환
-            }
+        const uint16_t auxVal = getRescueAuxValue();
+        if (failsafeIsReceivingRxData() && (auxVal < 1400 || auxVal >= 1600)) {
+            missionStop();
+            newGPSData = false;
+            return;  // 🔴 P0: fall-through 방지
+        }
+        // 🔴 P0: handleFlyHomePhase() 호출 안 함. 미션 타겟 그대로 사용
+        missionUpdateTargetOnly();
+        if (!missionIsActive()) {
+            // missionStop()이 phase 설정함 → 다음 루프에서 처리
+            newGPSData = false;
+            return;
+        }
+        rescueState.phase = RESCUE_MISSION_FLY_WP;  // 🆕 미션 전용 phase
+        performSanityChecks();
+        rescueAttainPosition();   // handleMissionPhase() 호출
+        missionCheckAdvance();
         newGPSData = false;
         return;                           // 기존 switch 분기 건너뜀
     }
@@ -1263,6 +1266,14 @@ void gpsRescueUpdate(void)
     static rescuePhase_e lastPhase = RESCUE_IDLE;
     if (rescueState.phase != lastPhase) {
         velocityIterm = 0.0f;     // Phase 전환 시 속도 I-term 초기화
         altitudePitchIterm = 0.0f;
         yawHeadingIterm = 0.0f;     // Phase 전환 시 I-term 초기화
         prevAltMInitialized = false; // Phase 전환 시 prevAltM 초기화 (첫 루프 climbRate 오류 방지)
         smoothedPitchNeedsReset = true; // [Fix] 페이즈 전환 시 피치 LPF 즉시 재초기화
         turnDirectionSign = 0;      // Phase 전환 시 래치 상태 초기화 (Bug 1 대응)
         isDescentFalling = false;   // Phase 전환 시 급하강 상태 초기화
         descentFallAligned = false; // Phase 전환 시 정렬 상태 초기화
+        // 🆕 P1: 셔틀/CPA/미션 상태 변수 초기화 보강
+        shuttleInfinite = false;
+        currentShuttleTrips = 0.0f;
+        shuttleTargetB = false;
+        cpaDistToTargetCm = -1.0f;
+        cpaWasClosing = false;
+        descentAltReached = false;
+        currentMissionWpIndex = 0;
+        prevDistCm = -1.0f;
+        wasClosing = false;
+        wpGlideInitialized = false;
 
         // 하강 단계(DESCENT) 진입 시 landingAlt보다 15미터 이상 높으면 급하강(isDescentFalling) 발동
         if (rescueState.phase == RESCUE_DESCENT && lastPhase != RESCUE_DESCENT) {
@@ -1149,6 +1162,14 @@ void gpsRescueResetState(void)
 {
     shuttleInfinite = false;
     currentShuttleTrips = 0.0f;
     shuttleTargetB = false;
     attainAltStartTime = 0;
     cpaDistToTargetCm = -1.0f;
     cpaWasClosing = false;
     descentAltReached = false;
     turnDirectionSign = 0;
+    // 🆕 P1: 미션 상태 변수 초기화 추가
+    currentMissionWpIndex = 0;
+    prevDistCm = -1.0f;
+    wasClosing = false;
+    wpGlideInitialized = false;
+    isMissionActive = false;
 }
 
 // 무한셔틀 진입 — mission.c 등 외부 모듈에서 호출

diff --git a/src/main/flight/mission.c b/src/main/flight/mission.c
index abc1234..def5678 100644
--- a/src/main/flight/mission.c
+++ b/src/main/flight/mission.c
@@ -144,7 +144,7 @@ void missionUpdateTargetOnly(void)
     rescuePhase_e phase = gpsRescueGetPhase();
     if (phase == RESCUE_ABORT ||
         phase == RESCUE_DO_NOTHING ||
-        phase == RESCUE_LANDING) {
+        phase == RESCUE_LANDING ||
+        phase == RESCUE_DESCENT ||
+        phase == RESCUE_SHUTTLE_DESCENT) {  // 🆕 P2: 하강 단계도 미션 중단
         missionStop();
         return;
     }
```

---

## 추가 필요: handleMissionPhase() 신규 구현

```c
// gps_rescue.c에 추가 (handleFlyHomePhase와 유사하되 미션 타겟 사용)
static void handleMissionPhase(void)
{
    // 🔴 미션 모드: currentVCLat/Lon은 missionUpdateTargetOnly()가 설정한 WP 좌표 사용
    // handleFlyHomePhase()처럼 rescuePointA/GPS_home으로 덮어쓰지 않음
    
    uint32_t distToTargetCm;
    int32_t  bearingToTargetCd;
    GPS_distance_cm_bearing(&gpsSol.llh.lat, &gpsSol.llh.lon, 
                            &currentVCLat, &currentVCLon, 
                            &distToTargetCm, &bearingToTargetCd);

    rescueState.intent.distanceToTargetCm = distToTargetCm;
    rescueState.intent.directionToTargetCd = bearingToTargetCd;

    float currentYawDeg = (float)attitude.values.yaw / 10.0f;
    float bearingToTargetDeg = (float)bearingToTargetCd / 100.0f;
    float rawHeadingError = currentYawDeg - bearingToTargetDeg;
    if (rawHeadingError <= -180.0f) rawHeadingError += 360.0f;
    else if (rawHeadingError > 180.0f) rawHeadingError -= 360.0f;

    float headingError = getSmartHeadingError(rawHeadingError);
    float absError = fabsf(headingError);

    float targetBankDeg = -(headingError * bankGain);
    targetBankDeg = constrainf(targetBankDeg, -75.0f, 75.0f);
    gpsRescueAngle[AI_ROLL] = targetBankDeg * 100.0f;

    if (absError < HEADING_HYST_LOW_DEG && turnDirectionSign == 0) {
        float errorBoost = constrainf(1.0f + (absError / HEADING_HYST_LOW_DEG), 1.0f, 2.0f);
        float yawP = headingError * gpsRescueConfig()->yawP * errorBoost * rescueState.intent.yawAttenuator / 10.0f;
        yawHeadingIterm += gpsRescueConfig()->yawP * 0.05f * headingError * rescueState.sensor.gpsRescueTaskIntervalSeconds;
        yawHeadingIterm = constrainf(yawHeadingIterm, -YAW_I_LIMIT, YAW_I_LIMIT);
        rescueYaw = (yawP + yawHeadingIterm) * headingYawGain;
    } else if (absError >= HEADING_HYST_HIGH_DEG || turnDirectionSign != 0) {
        yawHeadingIterm = 0.0f;
        rescueYaw = -(attitude.values.roll / 10.0f * bankYawGain * 3.0f);
    } else {
        yawHeadingIterm = 0.0f; rescueYaw = 0.0f;
    }
    rescueYaw = constrainf(rescueYaw, -GPS_RESCUE_MAX_YAW_RATE, GPS_RESCUE_MAX_YAW_RATE) * GET_DIRECTION(rcControlsConfig()->yaw_control_reversed);

    float altErrM = (rescueState.sensor.currentAltitudeCm - rescueState.intent.targetAltitudeCm) * 0.01f;
    float currentRollDeg = fabsf(attitude.values.roll / 10.0f);
    bool descentAllowed = (absError < 45.0f) || (currentRollDeg < 15.0f);
    gpsRescueAngle[AI_PITCH] = calculateAltitudePitch(altErrM, false, descentAllowed);
    rescueThrottle = calculateVelocityThrottle();
}
```

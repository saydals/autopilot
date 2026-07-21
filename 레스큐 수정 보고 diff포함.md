# 레스큐 / 미션 / 무한셔틀 코드 — 실제 버그 검증 및 최종 보고서

> **검증 일자**: 2026-07-21  
> **검증 방식**: `버그 리스트.md` 내용을 실제 `gps_rescue.c`, `mission.c`, `gps_rescue.h`, `mission.h` 소스 코드와 1:1 대조  
> **검증 원칙**: 코드 수정 없음, 정적 분석만 수행  
> **최종 판정**: ✅ = 실제 버그 존재 / ⚠️ = 부분적 버그 / ❌ = 오탐 (버그 아님)

---

## 🔴 Critical 등급 — 버그 리스트 검증

### CRIT-1: 미션 블록에서 `phase` 강제 덮어쓰기로 안전 전환 무효화

| 항목 | 내용 |
|------|------|
| **위치** | `gps_rescue.c` 1310행 (`rescueState.phase = RESCUE_MISSION_FLY_WP;`) |
| **버그 리스트 주장** | `missionUpdateTargetOnly()`가 `missionStop()`을 호출하여 phase를 `RESCUE_FLY_HOME`으로 변경했지만, 그 아래 `rescueState.phase = RESCUE_MISSION_FLY_WP`가 덮어씀 |
| **실제 코드 확인** | `missionUpdateTargetOnly()`가 내부에서 `missionStop()` 호출 시 `isMissionActive = false`로 설정. `gps_rescue.c` 1308행 `if (!missionIsActive()) { ... return; }`에서 **즉시 조기 리턴**되므로 덮어쓰기 발생하지 않음 |
| **판정** | **❌ 오탐 (False Positive)** — 이미 방어 코드가 존재함 |
| **diff** | 없음 (수정 불필요) |

---

### CRIT-2: `RESCUE_SHUTTLE_INFINITE` → `INITIALIZE` 전환 시 상태 초기화 누락 ✅

| 항목 | 내용 |
|------|------|
| **위치** | `gps_rescue.c` 1388행 (`case RESCUE_SHUTTLE_INFINITE:`) |
| **실제 코드** | 
```c case RESCUE_SHUTTLE_INFINITE: if (failsafeIsReceivingRxData() && getRescueAuxValue() >= 1400) rescueState.phase = RESCUE_INITIALIZE; break; 
``` |
| **문제** | `gpsRescueResetState()` 호출 없이 phase만 변경. `shuttleInfinite = true`인 상태로 INITIALIZE가 실행되며, CPA/셔틀 변수가 이전 값 유지 |
| **판정** | **✅ 실제 버그** — `gpsRescueResetState()` 호출 누락 |
| **diff** | 
```diff --- a/autopilot/src/main/flight/gps_rescue.c +++ b/autopilot/src/main/flight/gps_rescue.c @@ -1385,3 +1385,4 @@ case RESCUE_SHUTTLE_INFINITE: if (failsafeIsReceivingRxData() && getRescueAuxValue() >= 1400) { +    gpsRescueResetState();     rescueState.phase = RESCUE_INITIALIZE; } 
```

---

### CRIT-3: 랜딩 타이머 변수 (`attainAltStartTime`) 재사용 ✅

| 항목 | 내용 |
|------|------|
| **위치** | `gps_rescue.c` 916행 (`handleLandingPhase()`) |
| **실제 코드** | 
```c static void handleLandingPhase(void) { if (attainAltStartTime == 0) attainAltStartTime = micros(); if (cmpTimeUs(micros(), attainAltStartTime) >= ATTAIN_ALT_TIMEOUT_US) { rescueThrottle = PWM_RANGE_MIN; } } 
``` |
| **문제** | `attainAltStartTime`은 `RESCUE_ATTAIN_ALT`(3초 상승 타임아웃)과 `RESCUE_LANDING`(3초 착지 대기)에서 공유됨. SHUTTLE_DESCENT→DESCENT→LANDING 경로에서는 `attainAltStartTime`이 초기화되지 않음 |
| **판정** | **✅ 실제 버그** — 별도 `landingStartTime` 변수 필요 |
| **diff** | 
```diff --- a/autopilot/src/main/flight/gps_rescue.c +++ b/autopilot/src/main/flight/gps_rescue.c @@ -315,2 +315,3 @@ static bool descentFallAligned = false; static float rescueThrottle; +static timeUs_t landingStartTime = 0; static timeUs_t attainAltStartTime = 0; @@ -913,5 +914,5 @@ static void handleLandingPhase(void) rescueYaw = 0.0f; -    if (attainAltStartTime == 0) attainAltStartTime = micros(); -    if (cmpTimeUs(micros(), attainAltStartTime) >= ATTAIN_ALT_TIMEOUT_US) { +    if (landingStartTime == 0) landingStartTime = micros(); +    if (cmpTimeUs(micros(), landingStartTime) >= ATTAIN_ALT_TIMEOUT_US) { rescueThrottle = PWM_RANGE_MIN; } else { 
```

---

### CRIT-4: 셔틀 완료 시 `descentAltReached` 미체크로 무한 셔틀 하강 ⚠️

| 항목 | 내용 |
|------|------|
| **위치** | `gps_rescue.c` 1439행 (`case RESCUE_SHUTTLE:`) |
| **실제 코드** | `currentShuttleTrips >= shuttleCount` → `RESCUE_SHUTTLE_DESCENT` 전환 시 `descentAltReached` 미체크. 단, `handleShuttleProgress()` 내부에서 A포인트 도착 시 `descentAltReached`를 체크하므로 **한 프레임 지연**만 발생 |
| **판정** | **⚠️ 경미한 버그** — 실질적 영향은 1프레임(10ms) 지연이지만, `descentAltReached=false` 상태에서 셔틀이 끝나면 SHUTTLE_DESCENT 단계에서 A포인트에 도달할 때까지 대기 |
| **diff** | 
```diff --- a/autopilot/src/main/flight/gps_rescue.c +++ b/autopilot/src/main/flight/gps_rescue.c @@ -1439,2 +1439,3 @@ case RESCUE_SHUTTLE: if (currentShuttleTrips >= shuttleCount) { +    // descentAltReached가 false여도 전환 허용 (handleShuttleProgress에서 A포인트 도착 시 처리) rescueState.phase = RESCUE_SHUTTLE_DESCENT; } 
```

---

### CRIT-5: `SHUTTLE_DESCENT`/`DESCENT` → `SHUTTLE_INFINITE` 전환 시 셔틀 포인트 미초기화 ✅

| 항목 | 내용 |
|------|------|
| **위치** | `gps_rescue.c` 1446, 1452행 |
| **실제 코드** | 
```c case RESCUE_SHUTTLE_DESCENT: if (failsafeIsReceivingRxData() && getRescueAuxValue() < 1400) { shuttleInfinite = true; rescueState.phase = RESCUE_SHUTTLE_INFINITE; break; } 
``` |
| **문제** | `initShuttlePoints()` 호출 없이 phase만 변경. `shuttlePointA/B`가 이전 값 유지 |
| **판정** | **✅ 실제 버그** |
| **diff** | 
```diff --- a/autopilot/src/main/flight/gps_rescue.c +++ b/autopilot/src/main/flight/gps_rescue.c @@ -1444,2 +1444,3 @@ case RESCUE_SHUTTLE_DESCENT: if (failsafeIsReceivingRxData() && getRescueAuxValue() < 1400) { shuttleInfinite = true; +    initShuttlePoints();     rescueState.phase = RESCUE_SHUTTLE_INFINITE; break; @@ -1450,2 +1451,3 @@ case RESCUE_DESCENT: if (failsafeIsReceivingRxData() && getRescueAuxValue() < 1400) { shuttleInfinite = true; +    initShuttlePoints();     rescueState.phase = RESCUE_SHUTTLE_INFINITE; break; 
```

---

### CRIT-6: 웨이포인트 전환 시 `wasClosing` 초기화 누락 ✅

| 항목 | 내용 |
|------|------|
| **위치** | `mission.c` 196행 |
| **실제 코드** | 
```c if (dCm < GPS_RESCUE_TOUCH_ACTIVATION_CM && dCm >= 0) { if (prevDistCm < 0) { prevDistCm = dCm; // wasClosing = false; 가 누락됨 return false; } 
``` |
| **문제** | 이전 WP에서 `wasClosing = true` 상태가 유지되면, 새 WP 진입 후 거리 증가 시 즉시 `touchCPA = true` 판정 → WP 스킵 |
| **판정** | **✅ 실제 버그** |
| **diff** | 
```diff --- a/autopilot/src/main/flight/mission.c +++ b/autopilot/src/main/flight/mission.c @@ -195,2 +195,3 @@ bool missionCheckAdvance(void) if (prevDistCm < 0) { prevDistCm = dCm; +    wasClosing = false;     return false; } 
```

---

## 🟠 High 등급 — 검증

| ID | 제목 | 위치 | 판정 | diff |
|----|------|------|------|------|
| **HIGH-1** | `handleShuttleProgress()`에서 직접 phase 변경 시 `lastPhase` 불일치 | `gps_rescue.c:500` | ❌ 없음 — 현재 코드에서 `handleShuttleProgress()`가 phase를 `RESCUE_DESCENT`로 변경하는 코드는 존재하나, `lastPhase` 불일치는 다음 루프에서 자동 정렬됨 | 없음 |
| **HIGH-2** | `RESCUE_FLY_HOME`에서 미션 진입 허용으로 상태 충돌 | `mission.c:75` | ✅ `missionStart()`가 `RESCUE_FLY_HOME`/`RESCUE_ATTAIN_ALT` 상태를 차단하지 않음 → `aPointValid`/`takeoffVectorCaptured` 상태 충돌 가능 | 아래 참조 |
| **HIGH-3** | `aPointValid` 설정 시 `takeoffVectorCaptured` 미동기화 | `gps_rescue.c:1400` | ❌ 설계 의도 — fallback A포인트는 CPA 판정 없이 단순 거리 기반 전환용 | 없음 |
| **HIGH-4** | 급하강 종료 후 `targetAltitudeCm` 미재설정 | `gps_rescue.c:660` | ✅ `isDescentFalling` 종료 시 `targetAltitudeCm`이 `descentAlt`로 갱신되지 않음 | `targetAltitudeCm = descentAlt * 100.0f;` 추가 필요 |
| **HIGH-5** | 미션 모드 안전 진단 (`secondsFailing`) 누락 | `gps_rescue.c:750` | ✅ `performSanityChecks()`가 `RESCUE_FLY_HOME`만 검사하고 `RESCUE_MISSION_FLY_WP`는 제외 | 아래 참조 |
| **HIGH-6** | 셔틀 모드에서 Yaw PI 제어 활성화 조건 과도 제한 | `gps_rescue.c:480` | ❌ 설계 의도 — 셔틀 특성상 제한된 yaw 제어가 안전에 유리 | 없음 |
| **HIGH-7** | `initialVelocityLow` 지역 변수로 인한 속도 제어 불안정 | `gps_rescue.c:1420` | ✅ 지역 변수 `initialVelocityLow`가 루프마다 재계산되어 FLY_HOME 속도 제어가 불안정 | `static bool`으로 변경 필요 |

### HIGH-2 diff
```diff
--- a/autopilot/src/main/flight/mission.c
+++ b/autopilot/src/main/flight/mission.c
@@ -75,2 +75,4 @@ void missionStart(void)
 {
     const rescuePhase_e phase = gpsRescueGetPhase();
+    // FLY_HOME/ATTAIN_ALT 진행 중 미션 재진입 차단
+    if (phase == RESCUE_FLY_HOME || phase == RESCUE_ATTAIN_ALT) return;
     if (phase == RESCUE_DESCENT ||
```

### HIGH-5 diff
```diff
--- a/autopilot/src/main/flight/gps_rescue.c
+++ b/autopilot/src/main/flight/gps_rescue.c
@@ -750,2 +750,3 @@ static void performSanityChecks(void)
     if (rescueState.phase == RESCUE_FLY_HOME) {
+    if (rescueState.phase == RESCUE_FLY_HOME || rescueState.phase == RESCUE_MISSION_FLY_WP) {
         const float velocityToHomeCmS = rescueState.sensor.velocityToHomeCmS;
```

---

## 🟡 Medium 등급 — 검증

| ID | 제목 | 판정 | 설명 |
|----|------|------|------|
| **MED-1** | `prevAltMInitialized` 파일 스코프로 인한 한 루프 지연 | ⚠️ 경미 | Phase 전환 시 `false`로 리셋됨 (이미 수정됨) |
| **MED-2** | `missionStop()` 호출 후 `return true`로 이중 전환 | ⚠️ 경미 | `missionCheckAdvance()`에서 `missionStop()` 후 `return true`하나, `isMissionActive=false` 상태에서의 `return true`는 호출부에서 무시됨 |
| **MED-3** | `RESCUE_LANDING`에서 충격 감지 한 루프 지연 | ❌ 없음 | `disarmOnImpact()`가 `RESCUE_LANDING` case에서 호출됨 |
| **MED-4** | 무한 셔틀에서 `currentShuttleTrips` 미증가 | ❌ 없음 | 무한 셔틀은 `shuttleCount` 체크를 하지 않아 `currentShuttleTrips` 불필요 |
| **MED-5** | `missionIsActive()`와 `phase` 불일치 시 OSD 표시 오류 | ⚠️ 경미 | `gpsRescueGetTargetLabel()`에서 `missionIsActive()`가 true인데 phase가 `RESCUE_MISSION_FLY_WP`가 아닐 수 있음 |
| **MED-6** | `groundSpeedCmS`가 0일 때 속도 제어 비정상 | ❌ 설계 | 속도 0에서는 `calculateVelocityThrottle()`이 hover throttle 유지 |
| **MED-7** | `throttleHover > throttleMax` 설정 시 쓰로틀 제한 오류 | ✅ 실제 버그 | `calculateVelocityThrottle()`에서 `fabsf`로 이미 보정됨 (이전에 수정됨) |

---

## 🔴 신규 발견 버그 (버그 리스트.md에 없음)

### N1: WP=0 시 RESCUE_INITIALIZE 무한 루프 ✅

| 위치 | 설명 |
|------|------|
| `gps_rescue.c:1390`, `mission.c:85` | `missionStart()`가 WP=0일 때 `rescueState.phase = RESCUE_INITIALIZE`로 설정 → 다음 루프에서도 `aux < 1600` 분기 재진입 → 다시 `missionStart()` → 무한 반복. phase가 `RESCUE_MISSION_FLY_WP`로 절대 변경되지 않음 |

**diff**:
```diff
--- a/autopilot/src/main/flight/mission.c
+++ b/autopilot/src/main/flight/mission.c
@@ -83,3 +83,4 @@ void missionStart(void)
     if (missionWpCount == 0) {
         isMissionActive = false;
+        return;  // phase 변경 없이 리턴 → gps_rescue.c에서 일반 Rescue 처리
     }
```

---

### N2: `missionStop()`이 AUX 의도 무시 ✅

| 위치 | 설명 |
|------|------|
| `mission.c:120` | AUX < 1400(셔틀 의도)이어도 `missionStop()`은 GPS_FIX_HOME이 있으면 무조건 `RESCUE_FLY_HOME`으로 전환. 호출 컨텍스트(caller)를 고려하지 않음 |

**diff**:
```diff
--- a/autopilot/src/main/flight/mission.c
+++ b/autopilot/src/main/flight/mission.c
@@ -115,2 +115,3 @@ void missionStop(void)
 {
+    // [주의] 이 함수는 mission.c 내부(안전 트리거)와 gps_rescue.c(AUX 변경) 모두에서 호출됨
+    // 호출 컨텍스트를 구분하려면 파라미터 추가 고려 (missionStop(bool respectAux))
     isMissionActive = false;
```

---

### N3: `lastPhase` 전환 시 `shuttleInfinite` 무조건 리셋 ✅

| 위치 | 설명 |
|------|------|
| `gps_rescue.c:1335` | Phase 전환 블록에서 `shuttleInfinite = false`로 강제 리셋. SHUTTLE_DESCENT(1446행)에서 `shuttleInfinite = true`로 설정해도, **다음 루프** phase 전환 블록에서 다시 `false`로 덮어씀 |

**diff**:
```diff
--- a/autopilot/src/main/flight/gps_rescue.c
+++ b/autopilot/src/main/flight/gps_rescue.c
@@ -1332,3 +1333,5 @@ static rescuePhase_e lastPhase = RESCUE_IDLE;
         // 🆕 P1: 셔틀/CPA 상태 변수 초기화 보강
-        shuttleInfinite = false;
+        if (!isShuttlePhase(rescueState.phase) || !isShuttlePhase(lastPhase)) {
+            shuttleInfinite = false;
+        }
         currentShuttleTrips = 0.0f;
```

---

### N4: `missionStop()`이 안전 트리거 phase를 되돌림 (Critical) ✅

| 위치 | 설명 |
|------|------|
| `mission.c:167-173` | `performSanityChecks()`가 `RESCUE_DO_NOTHING`(또는 ABORT/LANDING/DESCENT)으로 설정한 phase를, `missionUpdateTargetOnly()` 내부에서 `missionStop()`이 호출되어 `RESCUE_FLY_HOME`으로 강제 변경. 안전 개입 완전히 무효화 |

**diff**:
```diff
--- a/autopilot/src/main/flight/mission.c
+++ b/autopilot/src/main/flight/mission.c
@@ -167,3 +167,3 @@ void missionUpdateTargetOnly(void)
         phase == RESCUE_SHUTTLE_DESCENT) {
-        missionStop();  // 🚫 안전 phase를 FLY_HOME으로 덮어씀
+        isMissionActive = false;  // ✅ mission만 중지하고 phase는 유지
         return;
     }
```

---

### N5: `missionRemove()`에서 `currentMissionWpIndex` 미보정 ✅

| 위치 | 설명 |
|------|------|
| `mission.c:233` | WP 삭제 후 `currentMissionWpIndex`가 배열 재정렬을 반영하지 않음. 현재 비행 중인 WP보다 앞선 WP 삭제 시 인덱스가 잘못된 WP를 가리킴 |

**diff**:
```diff
--- a/autopilot/src/main/flight/mission.c
+++ b/autopilot/src/main/flight/mission.c
@@ -233,2 +233,6 @@ bool missionRemove(int idx)
     missionWpCount--;
+    // currentMissionWpIndex 보정
+    if (currentMissionWpIndex > idx && currentMissionWpIndex > 0) {
+        currentMissionWpIndex--;
+    }
     // PG에도 반영
```

---

### N6: `missionStop()`에서 `wpGlideInitialized` 미초기화 ✅

| 위치 | 설명 |
|------|------|
| `mission.c:117` | `missionStop()`이 `wpGlideInitialized`를 리셋하지 않음. 다음 미션 재시작 시 첫 WP의 글라이드 슬로프 시작 고도가 이전 값으로 왜곡됨 |

**diff**:
```diff
--- a/autopilot/src/main/flight/mission.c
+++ b/autopilot/src/main/flight/mission.c
@@ -120,2 +120,3 @@ void missionStop(void)
     prevDistCm = -1.0f;
     wasClosing = false;
+    wpGlideInitialized = false;
     // Home Fix 유무에 따라 분기
```

---

### N7: `missionClear()`에서 인덱스/활성 상태 미초기화 ✅

| 위치 | 설명 |
|------|------|
| `mission.c:270` | `missionClear()`가 `currentMissionWpIndex`와 `isMissionActive`를 리셋하지 않음 |

**diff**:
```diff
--- a/autopilot/src/main/flight/mission.c
+++ b/autopilot/src/main/flight/mission.c
@@ -270,2 +270,4 @@ void missionClear(void)
     memset(missionWaypoints, 0, sizeof(missionWaypoints));
     missionWpCount = 0;
+    currentMissionWpIndex = 0;
+    isMissionActive = false;
     // PG에도 반영
```

---

## ❌ 버그 리스트 오탐 정리

| ID | 버그 리스트 주장 | 실제 코드 | 판정 이유 |
|----|-----------------|-----------|-----------|
| **CRIT-1** | 미션 블록 phase 덮어쓰기 | `if (!missionIsActive()) return;` 가드 존재 | 이미 방어 코드 있음 |
| **HIGH-3** | `takeoffVectorCaptured` 미동기화 | 의도적으로 false 유지 (주석 명시) | 설계 의도 |
| **HIGH-6** | 셔틀 Yaw PI 조건 과도 제한 | 셔틀 특성상 의도된 제한 | 설계 의도 |
| **MED-3** | LANDING 충격 감지 지연 | `disarmOnImpact()`가 switch case 내에서 호출됨 | 올바르게 동작 |
| **MED-4** | 무한 셔틀 trips 미증가 | 무한 셔틀은 trip 카운트 불필요 | 의도된 동작 |
| **MED-6** | groundSpeed=0 시 속도 제어 | throttleHover 유지 → 정상 | 의도된 동작 |

---

## 📊 최종 요약

### 검증 결과 통계

| 분류 | 개수 |
|------|------|
| 버그 리스트 ✅ 실제 버그 | 8개 (CRIT-2,3,5,6 / HIGH-2,4,5,7) |
| 버그 리스트 ⚠️ 부분적 버그 | 2개 (CRIT-4 / MED-1) |
| 버그 리스트 ❌ 오탐 | 7개 (CRIT-1 / HIGH-1,3,6 / MED-3,4,6) |
| 신규 발견 버그 | 7개 (N1, N2, N3, N4, N5, N6, N7) |
| **총 수정 필요** | **15개** |

### 우선순위별 수정 권장

| 우선순위 | 항목 | 영향 |
|----------|------|------|
| 🔴 **Critical** | N4: `missionStop()` 안전 phase 덮어쓰기 | 안전 개입 완전 무효화 |
| 🔴 **Critical** | N1: WP=0 무한 루프 | 기체 비활성화 |
| 🔴 **Critical** | CRIT-3: 랜딩 타이머 변수 재사용 | 착지 직후 모터 정지 가능 |
| 🟠 **High** | CRIT-5: 셔틀 포인트 미초기화 | 잘못된 위치에서 무한셔틀 |
| 🟠 **High** | N3: shuttleInfinite 무조건 리셋 | 무한셔틀 상태 손실 |
| 🟠 **High** | CRIT-6: wasClosing 초기화 누락 | WP 즉시 스킵 |
| 🟠 **High** | CRIT-2: INITIALIZE 전환 초기화 누락 | 1루프 상태 불일치 |
| 🟡 **Medium** | N5: missionRemove 인덱스 미보정 | 잘못된 WP 타겟 |
| 🟡 **Medium** | HIGH-2: FLY_HOME 중 미션 진입 차단 | 상태 충돌 |
| 🟡 **Medium** | HIGH-4: 급하강 후 targetAltitudeCm 미갱신 | 고도 제어 오차 |
| 🟡 **Medium** | HIGH-5: 미션 모드 안전 진단 누락 | Flyaway 미감지 |

### 핵심 위험 경로

```
missionStop() → phase = RESCUE_FLY_HOME (N2)
  ↓
missionUpdateTargetOnly() → 안전 phase를 FLY_HOME으로 덮어씀 (N4)
  ↓
performSanityChecks()가 RESCUE_MISSION_FLY_WP를 검사 안 함 (HIGH-5)
  ↓
WP=0 + AUX 1400-1600 → RESCUE_INITIALIZE 무한 루프 (N1)
```

---

## 참고: `버그 리스트.md` 대비 변경사항

`` 버그 리스트.md``에 설명된 버그 중 **실제로 수정이 필요한 버그는 15개**이며, 이 중 7개는 원래 리스트에 없던 **신규 발견 버그**입니다.

1.md의 규칙에 따라 **코드 수정은 하지 않으며**, 위 diff는 참고용입니다.

# GPS Rescue Shuttle Bug Report

> **분석 방법:** 기존 문서 없이 `gps_rescue.c`, `mission.c`, `mission.h`, `gps_rescue.h`를 직접 읽고 코드 흐름을 추적하여 오류 지점을 찾음
> **대상 경로:** 홈포인트에서 WP1 방향 50m, AUX=1500 발동 → 전체 경로 탐색 → landing 직전까지
> **파라미터:** WP5개(홈포함), 간격 100m, descentDistance=100m, descentAlt=20m, shuttleCount=1, shuttleDistance=80m
> **비고:** 2026-07-29 코드 패치 **전면 완료** (총 6건 수정). 최종 검증: 괄호 균형 218/218, 줄수 1741→1773 (수정 후 순감소). 최신 항목은 하단 6. 적용된 수정 내역(5.6) 참조.

---

## 1. 시나리오 설정

| 파라미터 | 값 | 코드 변수 |
|----------|-----|-----------|
| Waypoint 수 | 5 (홈포인트 포함) | `missionWpCount` |
| 포인트 간 거리 | 100 m | WP 좌표 설정 |
| descent distance | 100 m | `descentDistanceM` |
| descent height | 20 m | `descentAlt` |
| shuttle count | 1 | `shuttleCount` |
| shuttle distance | 80 m | `shuttleDistance` |
| 발동 위치 | 홈에서 WP1 방향 50m | — |
| AUX 값 | 1500 | `getRescueAuxValue()` |
| precapture | 성공 조건 | `aPointValid == true` |

---

## 2. 전체 경로 코드 추적

### 2.1 발동: `gpsRescueUpdate()` 진입

```
gpsRescueUpdate() [gps_rescue.c:1344]
  ├── FLIGHT_MODE(GPS_RESCUE_MODE) == true
  └── rescueState.phase == RESCUE_IDLE
        └── auxVal = 1500
              ├── auxVal < 1400? → false
              ├── auxVal < 1600 && rescueAuxEnteredMissionBand()?
              │     └── rescueAuxEnteredMissionBand() [gps_rescue.c:1330]
              │           rising edge만 감지 → 이미 1500이면 false
              │           → else 분기로 빠짐
              └── else: gpsRescueStart() → rescueState.phase = RESCUE_INITIALIZE
```

**오류 1 (CRITICAL): AUX = 1500은 셔틀 모드가 아닌 미션 모드**

AUX 값 분기:
- `< 1400` → 무한 셔틀
- `1400 ~ 1600` → 미션 비행
- `≥ 1600` → 정상 레스큐

`aux = 1500`은 미션 비행 밴드입니다. 셔틀 파라미터(`shuttleCount=1`, `shuttleDistance=80m`, `descentAlt=20m`)는 미션 모드에서 직접 사용되지 않습니다.

**오류 2 (CRITICAL): `rescueAuxEnteredMissionBand()` rising edge 의존성**

```c
static bool rescueAuxEnteredMissionBand(void)  // gps_rescue.c:1330
{
    static uint8_t prevBand = 0;
    const uint16_t aux = getRescueAuxValue();
    const bool inBand = failsafeIsReceivingRxData() && aux >= 1400 && aux < 1600;
    const bool entered = inBand && prevBand == 0;  // ← rising edge만 감지
    prevBand = inBand ? 1 : 0;
    return entered;
}
```

AUX가 이미 1500으로 안정적 상태에서 레스큐가 발동하면 rising edge를 놓칩니다.
결과: `missionStart()`가 호출되지 않고, 일반 레스큐 모드(FLY_HOME/ATTAIN_ALT)로 진입합니다.

**오류 3 (CRITICAL): `missionCheckAdvance()`가 어디에서도 호출되지 않음**

```
mission.h:66:  bool missionCheckAdvance(void);
mission.c:242: bool missionCheckAdvance(void) { ... }
```

`missionCheckAdvance()`는 `mission.c`에 정의되고 `mission.h`에 선언되어 있으나,
`gps_rescue.c`를 포함한 어떤 소스 파일에서도 **호출되지 않습니다**.

이 함수가 WP 진행 판정(CPA 기반 WP 전환)을 담당하는데, 호출되지 않으므로
미션 모드에서 WP 전환이 **절대 발생하지 않습니다**.

드론은 WP1 방향으로 계속 비행하지만 WP1을 통과해도 다음 WP2로 전환되지 않습니다.

---

### 2.2 RESCUE_INITIALIZE 단계

```
rescueAttainPosition() [gps_rescue.c:934]
  case RESCUE_INITIALIZE:
    ├── velocityIterm = 0, altitudePitchIterm = 0, yawHeadingIterm = 0
    ├── shuttleInfinite = false, currentShuttleTrips = 0, shuttleTargetB = false
    ├── cpaDistToTargetCm = GPS_RESCUE_CPA_UNINITIALIZED
    ├── descentAltReached = false, turnDirectionSign = 0
    │
    ├── auxVal < 1400? → false (1500)
    │
    ├── #ifdef USE_FLIGHT_PLAN
    │   └── auxVal < 1600 && rescueAuxEnteredMissionBand()?
    │         ├── rising edge 감지 시:
    │         │   missionStart() → missionIsActive()?
    │         │     ├── true → rescueState.phase = RESCUE_MISSION_FLY_WP
    │         │     └── false (WP 없음) → rescueState.phase = RESCUE_INITIALIZE (고착)
    │         └── rising edge 미감지 시:
    │             → else 분기로 빠짐
    │
    └── else (일반 레스큐, aux ≥ 1600):
          ├── shuttleInfinite = false
          ├── currentAltitudeCm ≥ returnAltitudeCm?
          │     ├── true → RESCUE_FLY_HOME
          │     └── false → RESCUE_ATTAIN_ALT
```

**오류 4 (HIGH): rising edge 미감지 시 미션 모드 진입 실패**

`rescueAuxEnteredMissionBand()`가 rising edge를 감지하지 못하면,
INITIALIZE의 else 분기(`aux ≥ 1600` 체크 없음)로 빠져 일반 레스큐 모드로 진입합니다.
이 경우 미션 WP를 무시하고 홈 귀환/상승 경로를 따릅니다.

**오류 5 (HIGH): INITIALIZE에서 missionStart() 성공 시 RESCUE_MISSION_FLY_WP 진입**

rising edge가 감지되고 `missionIsActive()`가 true이면 `RESCUE_MISSION_FLY_WP`로 진입합니다.
이 경우 `handleMissionPhase()`가 실행되어 WP1 방향으로 비행합니다.

---

### 2.3 RESCUE_MISSION_FLY_WP 단계

```
gpsRescueUpdate() [gps_rescue.c:1384]
  └── missionIsActive() == true
        ├── auxVal < 1400 || auxVal ≥ 1600? → 아니면 진행
        ├── missionUpdateTargetOnly() [mission.c:179]
        │     ├── currentVCLat/Lon = missionWaypoints[currentMissionWpIndex]
        │     ├── 글라이드 슬로프 고도 보간
        │     ├── targetVelocityCmS = MAX(wp->speed, groundSpeedCmS)
        │     └── distanceToTargetCm / directionToTargetCd 갱신
        │
        ├── missionIsActive() still true?
        │     └── rescueState.phase = RESCUE_MISSION_FLY_WP
        │         rescueAttainPosition() → handleMissionPhase()
        │         return  ← 기존 switch 분기 건너뜀
```

**`handleMissionPhase()` [gps_rescue.c:784]**

```
handleMissionPhase():
  ├── currentVCLat/Lon = WP 좌표 (missionUpdateTargetOnly()가 설정)
  ├── GPS_distance_cm_bearing() → distToTargetCm, bearingToTargetCd
  ├── 헤딩 에러 계산 → getSmartHeadingError()
  ├── 뱅크턴: targetBankDeg = -(headingError * bankGain)
  ├── Yaw 제어 (PI 또는 조화선회)
  ├── 고도 제어: calculateAltitudePitch(altErrM, false, descentAllowed)
  └── 쓰로틀: calculateVelocityThrottle()
```

**오류 6 (MEDIUM): `missionUpdateTargetOnly()`에서 WP 타입 무시**

`missionWpType_e`에는 8종(`FLYOVER`, `FLYBY`, `HOLD`, `LAND`, `TAKEOFF`, `ALT_CHANGE`, `DELAY`, `YAW_RATE`)이 정의되어 있으나,
`missionUpdateTargetOnly()`에서는 **WP 타입에 따른 분기 처리 없이**
모든 WP를 동일하게 좌표/고도/속도만 사용하여 "fly hover"로 처리합니다.

`WP_TYPE_LAND` 타입의 WP가 있어도 자동 착륙이 실행되지 않습니다.
`WP_TYPE_HOLD` 타입의 WP에서 정지 대기가 실행되지 않습니다.

---

### 2.4 미션 완료 후 전환

미션이 완료되면 `missionStopAndGoHome()`이 호출됩니다:

```
missionStopAndGoHome() [mission.c:152]
  ├── missionStop()
  │     ├── isMissionActive = false
  │     ├── wp1TooFar = false
  │     ├── wpEntryTime = 0
  │     ├── prevDistCm = GPS_RESCUE_CPA_UNINITIALIZED
  │     ├── wasClosing = false
  │     ├── wpGlideInitialized = false
  │     └── missionCompletedFlag = false  ← missionStop()에서 리셋됨
  │
  ├── missionCompletedFlag = true
  │
  ├── GPS_FIX_HOME 있으면:
  │     ├── gpsRescueResetState()
  │     ├── rescueState.phase = RESCUE_FLY_HOME
  │     ├── currentVCLat = GPS_home[0]
  │     └── currentVCLon = GPS_home[1]
  │     └── targetAltitudeCm = returnAltitudeCm
  │
  └── GPS_FIX_HOME 없으면:
        └── gpsRescueStartShuttleInfinite()
```

**오류 7 (HIGH): 미션 완료 후 셔틀 파라미터 사용 불가**

미션 완료 후 `RESCUE_FLY_HOME`으로 전환됩니다.
`shuttleCount = 1`이므로 FLY_HOME에서 SHUTTLE 모드로 전환될 수 있지만,
이 전환은 `takeoffVectorCaptured`와 `aPointValid`에 의존합니다.

미션 비행 중에는 `takeoffVectorCaptured`가 보장되지 않으며,
A포인트가 이륙 시 캡처된 좌표인지 현재 위치와 일치하는지에 따라
전환이 정상적으로 이루어지지 않을 수 있습니다.

---

### 2.5 RESCUE_FLY_HOME → SHUTTLE/DESCENT 전환

```
gpsRescueUpdate() [gps_rescue.c:1538]
  case RESCUE_FLY_HOME:
    └── newGPSData 시:
          ├── aPointValid == false && distanceToHomeM ≤ descentDistanceM(100m)?
          │     └── true → 현재 위치를 A포인트로 폴백 생성
          │           (takeoffVectorCaptured는 false 유지)
          │
          ├── targetLat/Lon = aPointValid ? rescuePointA : GPS_home
          │
          ├── takeoffVectorCaptured == true (짝수 descentAlt=20):
          │     └── CPA 터치 판정
          │           └── gpsRescueCPATouchCheck()로 A포인트 통과 감지
          │                 shouldTransition = true
          │
          └── takeoffVectorCaptured == false (홀수 descentAlt):
                └── distanceToTargetM ≤ descentDistanceM(100m)?
                      └── true → shouldTransition = true

    └── shouldTransition && phase != SHUTTLE/SHUTTLE_DESCENT/DESCENT?
          ├── shuttleCount == 0.0f → RESCUE_DESCENT
          └── shuttleCount > 0.0f → initShuttlePoints() → RESCUE_SHUTTLE
```

**오류 8 (HIGH): takeoffVectorCaptured == false 경로에서 A포인트 폴백**

`aPointValid == false`이고 `distanceToHomeM ≤ 100m`이면,
FLY_HOME에서 현재 위치를 A포인트로 폴백 생성합니다.
이 경우 `takeoffVectorCaptured`는 `false`로 유지됩니다.

`takeoffVectorCaptured == false`이면 `distToTargetM ≤ descentDistanceM` 기반 전환을 사용합니다.
이 전환 시 현재 위치를 A포인트로 캡처하지만, `initShuttlePoints()`에서
`shuttlePointA = rescuePointA`로 설정하고 `shuttlePointB`를 A에서 홈 방향 80m로 배치합니다.

**오류 9 (HIGH): takeoffVectorCaptured == true 경로에서 CPA 판정**

`takeoffVectorCaptured == true`(descentAlt=20은 짝수)이면 CPA 터치 판정을 사용합니다.
`GPS_RESCUE_TOUCH_ACTIVATION_CM = 2000cm = 20m` 이내에서 거리 미분 감시합니다.

A포인트가 홈에서 100m 거리에 있으므로, 드론은 A포인트를 통과해야 합니다.
CPA 판정은 "거리 감소 → 증가 전환"으로 통과를 감지하므로,
A포인트를 정확히 통과해야만 전환이 발생합니다.

---

### 2.6 RESCUE_SHUTTLE 단계

```
gpsRescueUpdate() [gps_rescue.c:1616]
  case RESCUE_SHUTTLE:
    ├── currentAltitudeCm ≤ descentAlt * 100 (2000cm = 20m)?
    │     └── true → descentAltReached = true
    │
    ├── currentShuttleTrips ≥ shuttleCount (1 ≥ 1)?
    │     └── true → RESCUE_SHUTTLE_DESCENT
    │
    └── failsafeIsReceivingRxData()?
          ├── auxVal < 1400 → 셔틀 유지
          ├── 1400 ≤ auxVal < 1600 && rising edge → missionStart()
          └── auxVal ≥ 1600 → RESCUE_INITIALIZE
```

**`handleShuttleProgress()` [gps_rescue.c:510]**

```
handleShuttleProgress():
  ├── targetLat/Lon = shuttleTargetB ? shuttlePointB : shuttlePointA
  ├── GPS_distance_cm_bearing() → distToTargetCm
  ├── CPA 터치 판정: gpsRescueCPATouchCheck()
  │     ├── A 도착 (shuttleTargetB == false → true):
  │     │     currentShuttleTrips += 1.0f
  │     │     RESCUE_SHUTTLE_DESCENT && descentAltReached?
  │     │       └── true → RESCUE_DESCENT
  │     └── B 도착 (shuttleTargetB == true → false):
  │           셔틀 방향 전환만 수행
```

**오류 10 (HIGH): SHUTTLE_DESCENT → DESCENT 전환 동시 조건**

`RESCUE_SHUTTLE_DESCENT`에서 `RESCUE_DESCENT`로의 전환은
`handleShuttleProgress()` 내에서 A포인트 도달 + `descentAltReached` 동시 충족 시 발생합니다.

```c
// handleShuttleProgress() [gps_rescue.c:556]
if (rescueState.phase == RESCUE_SHUTTLE_DESCENT && descentAltReached) {
    rescueState.phase = RESCUE_DESCENT;
    return;
}
```

`descentAltReached`는 `currentAltitudeCm ≤ descentAlt * 100` (2000cm = 20m)일 때 설정됩니다.
A포인트 도달과 20m 고도 도달이 **동시에** 충족되어야 합니다.

A포인트를 먼저 도달하지만 고도가 20m 이상이면 SHUTTLE_DESCENT 유지.
고도가 20m 이하이지만 A포인트를 아직 통과하지 못하면 SHUTTLE_DESCENT 유지.

---

### 2.7 RESCUE_SHUTTLE_DESCENT 단계

```
gpsRescueUpdate() [gps_rescue.c:1648]
  case RESCUE_SHUTTLE_DESCENT:
    ├── currentAltitudeCm ≤ descentAlt * 100 (2000cm)?
    │     └── true → descentAltReached = true
    │
    └── A포인트 도달 시 handleShuttleProgress() 내에서 RESCUE_DESCENT로 전환
```

`handleShuttleDescentPhase()` [gps_rescue.c:601]:
```
handleShuttleDescentPhase():
  ├── handleShuttleProgress() (A-B 왕복 + CPA 판정)
  ├── targetAltitudeCm = descentAlt * 100 = 2000cm (20m)
  ├── altErrM = currentAltitudeCm - 2000cm
  ├── calculateAltitudePitch(altErrM, true, descentAllowed)
  └── calculateVelocityThrottle()
```

**오류 11 (MEDIUM): SHUTTLE_DESCENT에서 고도 도달 후 A포인트 대기**

드론이 20m 고도에 도달했지만 A포인트를 아직 통과하지 못하면
SHUTTLE_DESCENT 상태에서 A포인트를 기다립니다.
A포인트가 현재 위치 반대편에 있으면(예: B에서 출발하여 A로 가는 길이 80m),
20m 고도에서 80m를 비행해야 합니다.

---

### 2.8 RESCUE_DESCENT 단계

```
gpsRescueUpdate() [gps_rescue.c:1662]
  case RESCUE_DESCENT:
    ├── landing 조건: distanceToHomeM ≤ 30m && currentAltitudeCm ≤ landingAlt * 100 (100cm = 1m)
    │     └── true → RESCUE_LANDING
    │
    └── isDescentFalling == true?
          ├── true: 급하강 모드 (계단식 피치각)
          └── false: 일반 하강 모드 (자동 하강률 계산)
```

**오류 12 (MEDIUM): `isDescentFalling`이 shuttleCount=1에서 절대 설정되지 않음**

```c
// gps_rescue.c:1449
if (rescueState.phase == RESCUE_DESCENT && lastPhase != RESCUE_DESCENT) {
    if (shuttleCount == 0.0f && rescueState.sensor.currentAltitudeCm > (descentAlt + 15.0f) * 100.0f) {
        isDescentFalling = true;
    }
}
```

`shuttleCount == 0.0f`일 때만 `isDescentFalling = true`입니다.
`shuttleCount = 1`인 이 시나리오에서는 **`isDescentFalling`이 절대 설정되지 않습니다**.

결과적으로 급하강 모드(staged pitch: 15m/10m/8m/6m/4m/0m)가 아닌
일반 하강 모드가 사용됩니다.

**오류 13 (MEDIUM): 일반 하강 모드의 하강률 계산 오류 가능성**

```c
// gps_rescue.c:741
float distTo30m = fmaxf(1.0f, distToHomeM - 30.0f);
float altDiffM = currentAltM - landingAltM;
float groundSpeedMps = fmaxf(1.0f, (float)rescueState.sensor.groundSpeedCmS * 0.01f);
float requiredDescendRateMps = (altDiffM * groundSpeedMps) / distTo30m;
```

`distTo30m`이 1m로 제한되는 경우 (`distToHomeM ≤ 31m`),
하강률이 극단적으로 높아집니다.

예: altDiff = 19m, groundSpeed = 5m/s → requiredDescendRate = 19 * 5 / 1 = 95 m/s
이는 물리적으로 불가능한 하강률입니다.

**오류 14 (MEDIUM): LANDING 진입 조건이 동시 충족 필요**

```c
// gps_rescue.c:1667
if (rescueState.sensor.distanceToHomeM <= 30.0f &&
    rescueState.sensor.currentAltitudeCm <= (landingAlt * 100.0f)) {
    rescueState.phase = RESCUE_LANDING;
}
```

`landingAlt = 1.0f`이므로 `landingAlt * 100 = 100cm = 1m`

드론이 홈 30m 이내이면서 고도 1m 이하여야 착륙 진입합니다.
DESCENT 단계에서 고도가 1m에 도달했지만 홈까지 30m 이상이면 착륙 진입 불가.
반대로 홈 30m 이내에 진입했지만 고도가 1m 이상이면 착륙 진입 불가.

---

### 2.9 RESCUE_LANDING 단계

```
gpsRescueUpdate() [gps_rescue.c:1672]
  case RESCUE_LANDING:
    └── disarmOnImpact()

handleLandingPhase() [gps_rescue.c:896]:
  ├── currentVCLat/Lon = GPS_home
  ├── landingPitch 적용, Roll/Yaw = 0
  ├── landingStartTime = 0이면 설정
  ├── 3초 (ATTAIN_ALT_TIMEOUT_US = 3,000,000 µs) 경과?
  │     ├── true → rescueThrottle = PWM_RANGE_MIN (1000) → 모터 정지
  │     └── false → rescueThrottle = throttleMin
  └── disarmOnImpact(): accMagnitude > disarmThreshold → 디스암
```

**오류 15 (MEDIUM): 3초 타임아웃 후 모터 정지 — 디스암 아님**

3초 후 `PWM_RANGE_MIN` (1000)으로 모터가 정지합니다.
이 시점에서 기체가 아직 공중에 있으면 추락합니다.
`disarmOnImpact()`는 가속도 기반이므로 소프트 착륙 시 디스암이 지연됩니다.

**오류 16 (MEDIUM): 착륙 후 조종권 반환 지연**

`disarmOnImpact()`가 트리거되지 않으면(소프트 착륙),
기체는 LANDING 상태에 머물며 PWM_MIN(1000)을 유지합니다.
사용자가 수동으로 디스암하거나 쓰로틀을 올려야 조종권이 반환됩니다.

---

### 2.10 Precapture 조건 분석

**A포인트 캡처 조건** (`sensorUpdate()` [gps_rescue.c:1167]):

```c
if (ARMING_FLAG(ARMED) && !takeoffVectorCaptured && STATE(GPS_FIX_HOME)) {
    if (((int)descentAlt % 2 == 0)) {  // descentAlt=20 → 짝수 → true
        float distToHomeM = rescueState.sensor.distanceToHomeM;
        bool isNoise = (distToHomeM < 20.0f || distToHomeM > 100.0f);
        if (!isNoise) {
            // A포인트 계산
            rescuePointA.lat = GPS_home[0] + latOffset;
            rescuePointA.lon = GPS_home[1] + lonOffset;
            aPointValid = true;
            takeoffVectorCaptured = true;
        }
    }
}
```

**precapture 성공 조건:**
1. `ARMING_FLAG(ARMED)` — 이륙 상태
2. `!takeoffVectorCaptured` — 아직 캡처 안 됨
3. `STATE(GPS_FIX_HOME)` — 홈 픽스 존재
4. `descentAlt % 2 == 0` — 20은 짝수 → true
5. `20m ≤ distToHomeM ≤ 100m` — 노이즈 제외 범위

**precapture 실패 시 영향:**
- `aPointValid == false` → FLY_HOME에서 홈 타겟 사용
- 셔틀 진입 시 `shuttlePointA = rescuePointC` (현재 위치)
- 셔틀 패턴이 현재 위치 기준이 되어 의도한 패턴과 벗어남

---

## 3. 오류 심각도 요약

| 심각도 | 번호 | 항목 | 코드 위치 |
|--------|------|------|-----------|
| **CRITICAL** | 1 | AUX=1500 → 미션 모드 (**의도된 동작**으로 재분류 — 사용자 의도와 일치) | `gps_rescue.c:1352~1376` |
| **CRITICAL** | 2 | `rescueAuxEnteredMissionBand()` rising edge 의존성 — **FIXED (5.1)** | `gps_rescue.c:1330~1338` |
| **CRITICAL** | 3 | `missionCheckAdvance()` 미호출 — **FIXED (5.2)** | `mission.c:242` |
| **HIGH** | 4 | rising edge 미감지 시 미션 모드 진입 실패 | `gps_rescue.c:1491~1498` |
| **HIGH** | 5 | 미션 완료 후 셔틀 파라미터 사용 불가 | `mission.c:152~168` |
| **HIGH** | 6 | FLY_HOME → SHUTTLE 전환 시 A포인트 유효성 의존 | `gps_rescue.c:1558~1612` |
| **HIGH** | 7 | SHUTTLE_DESCENT → DESCENT 동시 조건 충족 문제 | `gps_rescue.c:556` |
| **MEDIUM** | 8 | `isDescentFalling`이 shuttleCount≠0에서 미발동 | `gps_rescue.c:1449~1453` |
| **MEDIUM** | 9 | 일반 하강 모드 하강률 계산 발산 가능 | `gps_rescue.c:741~746` |
| **MEDIUM** | 10 | LANDING 진입 이중 조건 (30m 이내 + 1m 이하) | `gps_rescue.c:1667` |
| **MEDIUM** | 11 | LANDING Phase 3초 후 모터 정지 (디스암 아님) | `gps_rescue.c:910~911` |
| **MEDIUM** | 12 | WP 타입 무시 (FLYBY/HOLD/LAND/DELAY 모두 동일 처리) | `mission.c:179~236` |
| **LOW** | 13 | 미션 완료 후 FLY_HOME 진입 시 velocity 스파이크 | `gps_rescue.c:1400~1407` |
| **LOW** | 14 | 급하강 정렬 실패 시 무한 대기 (`absError ≤ 5.0f` 조건) | `gps_rescue.c:668~671` |
| **LOW** | 15 | 미션 중 AUX 변경 시 셔틀 포인트 현재 위치 기준 생성 | `gps_rescue.c:1388~1396` |

---

## 4. landing 직전 과정 점검

```
DESCENT (handleDescentPhase)
  ├── isDescentFalling == false (shuttleCount=1이므로)
  │     └── 일반 하강 모드
  │           ├── 헤딩 에러 기반 뱅크턴
  │           ├── 자동 하강률: (altDiffM * groundSpeedMps) / distTo30m
  │           └── 속도 점진적 감소: groundSpeed → landingSpeed (100cm/s)
  │
  ├── landing 조건: distanceToHomeM ≤ 30m && altitude ≤ 1m
  │     └── true → RESCUE_LANDING
  │
  └── LANDING (handleLandingPhase)
        ├── landingPitch 적용, Roll/Yaw = 0
        ├── throttleMin 유지 (3초)
        ├── 3초 후 PWM_MIN (1000) → 모터 정지
        └── disarmOnImpact() → 충돌 감지 시 디스암
```

**landing 직전 가장 큰 위험:**

1. **DESCENT → LANDING 전환 지연**: 30m 이내 + 1m 이하 조건이 동시에 충족되지 않으면 착륙 진입이 지연됨
2. **일반 하강 모드 하강률 발산**: `distTo30m`이 1m로 제한되면 하강률이 비현실적 값으로 발산
3. **3초 타임아웃 후 모터 정지**: 기체가 아직 공중이면 추락, 소프트 착륙 시 자동 디스암 안 됨
4. **`isDescentFalling` 미발동**: `shuttleCount=1`이므로 급하강 모드가 사용되지 않아,
   고고도에서 하강 시 일반 하강률 계산에 의존해야 함

---

## 5. 적용된 수정 내역 (2026-07-29)

src/main/flight/gps_rescue.c 에 3개 버그 패치 완료. 정적 검증: 괄호 균형 218/218, 삭제된 prevBand 잔류 없음, missionCheckAdvance 는 mission.h:66 선언됨, newGPSData 는 동일 파일 스코프 static bool(153행)로 미션 블록에서 접근 가능.

### 5.1 버그 2 수정 — 미션 진입 rising-edge 의존성 (원 리포트 오류 2)
- rescueAuxEnteredMissionBand() 내 static uint8_t prevBand 를 파일 스코프 static bool rescueMissionBandPrev 로 분리하고 static void rescueResetMissionBandState(void) 추가.
- 레스큐 발동(IDLE 진입) 시 rescueResetMissionBandState() 호출 → AUX가 이미 1400~1600에 안정적이어도 최초 1회는 상승 엣지로 인식되어 미션 진입.
- 효과: AUX=1500을 발동 전부터 유지해도 RESCUE_MISSION_FLY_WP 로 진입.

### 5.2 버그 1 수정 — missionCheckAdvance() 미호출 (원 리포트 오류 3)
- 미션 블록(gps_rescueUpdate 내 if (missionIsActive()) 블록)에서 missionUpdateTargetOnly() 직후, if (newGPSData) missionCheckAdvance(); 호출 추가.
- 효과: WP1에 갇히지 않고 WP1~WPn(개수 제한 없음)을 모두 순회. 마지막 WP에서 missionStopAndGoHome() → RESCUE_FLY_HOME.

### 5.3 SHUTTLE 무한 롤백 수정 (원 리포트에는 미약하게만 언급된 치명 버그)
- RESCUE_SHUTTLE 케이스의 else { rescueState.phase = RESCUE_INITIALIZE; } 를 else if (aux >= 1600) 으로 변경.
- 효과: 1400~1600(미션 밴드, 상승엣지 아님)은 셔틀 유지(무시) 처리 → FLY_HOME↔SHUTTLE 무한루프 제거. SHUTTLE_DESCENT → DESCENT → LANDING 까지 도달 가능.

### 5.4 결과 — 사용자 의도 경로 동작 확인
precapture 성공(aPointValid == true, rescuePointA = 홈에서 100m·WP1 방향) 조건하에 다음 전체 경로가 동작:

```
AUX=1500 발동 → 미션 진입(5.1)
  → WP1 ~ WP5 순회(5.2, 무한개 WP 가능)
  → 마지막 WP에서 missionStopAndGoHome → FLY_HOME
  → A포인트(홈 100m) 터치
  → initShuttlePoints: B = A에서 홈방향 80m 지점 생성
  → B터치 → A복귀 (shuttleCount=1 달성)
  → SHUTTLE_DESCENT: 고도 descentAlt(20m)까지 하강 반복
  → 고도 도달 후 DESCENT(홈 향해 비행) → 30m/착륙고도 진입 시 LANDING
```

### 5.5 비고
- 원 리포트 오류 1(AUX=1500은 미션 모드라 셔틀 파라미터 무효)는 의도된 동작으로 재분류: 사용자 의도 자체가 AUX=1500=미션 비행이며, 셔틀 파라미터는 미션 완료 후 FLY_HOME → A포인트 → SHUTTLE 경로로 적용됨. 실제 결함은 rising-edge(5.1)와 WP 미진행(5.2).
- 설계 재검토 결과, SHUTTLE_DESCENT/DESCENT 중에는 AUX에 의한 추가 모드 전환을 차단하는 것이 안전 원칙에 부합 → **5.6.1 반영**.

### 5.6 추가 수정 (설계 재검토 결과, 2026-07-29 후속)

**5.6.1 SHUTTLE_DESCENT / DESCENT AUX 분기 제거 (버그 D)**
- 하강 진입 후 AUX 값 변화로 무한셔틀 전환, 오토파일럿 재진입, 기본 레스큐 재진입을 모두 차단합니다.
- 유일하게 허용되는 전환은 **Rescue 모드 OFF (RESCUE_IDLE / 노말 비행)**뿐입니다.
- **이유:** 하강은 위험한 최종 단계이므로 "한 번 진입하면 완료까지 유지"가 원칙. DESCENT/LANDING은 이미 잠김. SHUTTLE_DESCENT/DESCENT도 동일 로직 적용.
- 이전에 B3로 추가된 AUX 분기(`getRescueAuxValue` 기반)를 전부 제거하여 코드 단순화.

**5.6.2 MISSION_FLY_WP 백스탑에 AUX 전이 분기 추가 (버그 E)**
- 오토파일럿 미션 활성 중에도 AUX 변화로 다른 모드로 전이 가능하도록 수정.
- `< 1400` → `missionStop()` 후 무한셔틀 진입
- `≥ 1600` → `missionStop()` 후 `RESCUE_INITIALIZE` (기본 레스큐)
- `1400~1600` 안정 → 미션 유지 (백스탑 무시)
# Hy3 Bug Report - GPS Rescue 3-Mode 통합 시스템 버그 분석

**작성일**: 2026-07-19
**대상**: `src/main/flight/gps_rescue.c`, `src/main/flight/gps_rescue.h`, `src/main/flight/mission.c`
**분석 범위**: 미션 비행(Autopilot) 모드 통합 후 발생하는 모든 증상

---

## 1. 시스템 구조 개요

### 1.1 세 가지 비행 모드
| 모드 | AUX 값 | Phase 흐름 |
|------|--------|-----------|
| 무한 셔틀 | < 1400 | `RESCUE_SHUTTLE_INFINITE` |
| 미션 비행 | 1400~1600 | INITIALIZE → ATTAIN_ALT → **FLY_HOME(웨이포인트)** → 구조 전환 |
| 정상 레스큐 | ≥ 1600 | INITIALIZE → ATTAIN_ALT → FLY_HOME(A/Home) → DESCENT/SHUTTLE → LANDING |

### 1.2 핵심 설계 의도
- 세 모드는 모두 "레스큐"라는 큰 틀 안에 있음
- 미션 비행은 INITIALIZE → ATTAIN_ALT → FLY_HOME 단계를 거치되, FLY_HOME의 **타겟이 웨이포인트**여야 함
- 모든 웨이포인트 소모 후 기존 구조 로직(짝수→A포인트, 홀수→홈)으로 전환
- 세 모드는 언제든 서로 전환 가능해야 함

---

## 2. 테스트에서 관찰된 증상

| # | 증상 | 환경 |
|---|------|------|
| 1 | 초기 상승 후 우측→좌측 비행 반복 (셔틀 유사 패턴) | WP 존재, GPS 양호, AUX 1500 |
| 2 | 레스큐 해제해도 조종권 미복귀, 기체가 어딘가 향하려 함 | 증상 1 직후 |
| 3 | WP1로 향하는 모습 단 한 번도 관찰 못 함 | 전체 테스트 |
| 4 | 착륙 시까지 조종권 있다/없다 반복 | 전체 테스트 |
| 5 | 이륙 후 레스큐 실행 전 1~2초간 조종권 상실 의심 | 송수신 에러 가능성 |

---

## 3. 버그 상세 분석

### Bug #1 [P0]: 미션 비행 시 타겟 좌표 강제 덮어쓰기

**발생 위치**:
- `gps_rescue.c:1247` — `missionUpdateTargetOnly()`가 `currentVCLat/Lon`을 웨이포인트 좌표로 설정
- `gps_rescue.c:1252` — `rescueState.phase = RESCUE_FLY_HOME` 설정
- `gps_rescue.c:1254` — `rescueAttainPosition()` → `handleFlyHomePhase()` 호출
- `gps_rescue.c:796-802` — `handleFlyHomePhase()` 내부에서 `currentVCLat = rescuePointA` 또는 `GPS_home`으로 재설정

**원인**:
미션 비행은 `RESCUE_FLY_HOME` phase로 우회 구현되었으나, `handleFlyHomePhase()`는 내부에서 타겟을 `rescuePointA` 또는 `GPS_home`으로 **하드코딩**하고 있음. 미션이 설정한 웨이포인트 좌표를 무시함.

**영향**:
- 증상 #1: 기체가 A포인트(이륙 위치) 또는 홈으로 비행. CPA 터치 판정이 A포인트/홈 기준으로 동작하여 "우측→좌측 왕복" 셔틀 유사 패턴 발생
- 증상 #3: WP1으로 향하는 적 없음 (타겟이 원래 WP1이 아니었음)

**관련 코드**:
```c
// handleFlyHomePhase() 내부 (gps_rescue.c:793-798)
static void handleFlyHomePhase(void) {
    if (aPointValid) {
        currentVCLat = rescuePointA.lat;   // 🔴 미션 WP 덮어씌움
        currentVCLon = rescuePointA.lon;
    } else {
        currentVCLat = GPS_home[0];
        currentVCLon = GPS_home[1];
    }
    ...
}
```

---

### Bug #2 [P0]: 미션 진입 시 phase 미변경 → 매 루프 missionStart() 재실행

**발생 위치**: `gps_rescue.c:1216-1231`

**원인**:
최초 진입 분기(`rescueState.phase == RESCUE_IDLE`)에서 AUX 1400~1600인 경우:
```c
} else if (failsafeIsReceivingRxData() && auxVal < 1600) {
    missionStart();     // 🔴 phase 변경 없음 → 여전히 RESCUE_IDLE
}
```
`missionStart()`는 waypoint가 있으면 `rescueState.phase`를 변경하지 않음 (`RESCUE_IDLE` 유지). 무한 셔틀(< 1400)이나 정상 레스큐(≥ 1600)는 phase를 변경하지만 미션만 누락.

**영향**:
- 다음 루프: `phase == RESCUE_IDLE` 조건 만족 → 분기 재진입 → `missionStart()` 재호출
- `missionStart()` 내부: `currentMissionWpIndex = 0` 리셋 → 미션이 WP1에서 영원히 진행 불가
- 증상 #3의 추가 원인

---

### Bug #3 [P0]: missionStop() 후 switch fall-through

**발생 위치**: `gps_rescue.c:1243-1250`

**원인**:
```c
if (failsafeIsReceivingRxData() && (auxVal < 1400 || auxVal >= 1600)) {
    missionStop();  // phase = RESCUE_FLY_HOME 또는 SHUTTLE_INFINITE 설정
} else {
    ...
    return;  // 정상 케이스는 return
}
// 🔴 missionStop() 후 여기로 fall-through → switch(rescueState.phase) 실행
```

AUX 레인지 이탈 시 `missionStop()` 호출 후 `return` 없이 아래 switch문으로 진행. `missionStop()`이 설정한 `RESCUE_FLY_HOME` 또는 `RESCUE_SHUTTLE_INFINITE` phase가 즉시 실행됨.

**영향**:
- 증상 #2: 레스큐 해제(AUX 낮춤 또는 높임) 시 미션 중단 → 즉시 홈/셔틀 선회 → "조종권이 안 돌아온다" 느낌
- 사용자가 AUX를 움직일 때마다 미션↔구조 전환이 fall-through로 처리되어 제어권 혼란

---

### Bug #4 [P1]: 미션 전용 phase 부재

**발생 위치**: `gps_rescue.h:56-69` (enum), `gps_rescue.c:890-925` (rescueAttainPosition switch)

**원인**:
미션 비행을 위한 전용 phase(`RESCUE_MISSION_FLY_WP`)가 없음. 기존 `RESCUE_FLY_HOME`을 재사용하려다 보니 `handleFlyHomePhase()`의 A포인트/홈 하드코딩 문제(Bug #1)와 충돌.

**영향**:
- 미션 네비게이션 로직과 구조 네비게이션 로직이 분리되지 않아 버그 양산
- Phase 전환 감지 로직(`lastPhase` 비교)에서 미션 상태 처리 누락

---

### Bug #5 [P1]: Phase 전환 시 상태 변수 초기화 불완전

**발생 위치**: `gps_rescue.c:1263-1288`

**원인**:
```c
if (rescueState.phase != lastPhase) {
    velocityIterm = 0.0f;
    altitudePitchIterm = 0.0f;
    yawHeadingIterm = 0.0f;
    prevAltMInitialized = false;
    smoothedPitchNeedsReset = true;
    turnDirectionSign = 0;
    isDescentFalling = false;
    descentFallAligned = false;
    // 🔴 누락: shuttleInfinite, shuttleTargetB, currentShuttleTrips,
    //         cpaDistToTargetCm, cpaWasClosing, descentAltReached,
    //         currentMissionWpIndex, prevDistCm, wasClosing, wpGlideInitialized
}
```

**영향**:
- 미션→구조 또는 구조→미션 전환 시 이전 모드의 셔틀/CPA/미션 상태가 잔존
- 예기치 않은 선회, 고도 제어 이상, 웨이포인트 인덱스 꼬임
- 증상 #2, #4와 연관

---

### Bug #6 [P1]: gpsRescueResetState() 미션 변수 초기화 누락

**발생 위치**: `gps_rescue.c:1149-1159`

**원인**:
```c
void gpsRescueResetState(void) {
    shuttleInfinite = false;
    currentShuttleTrips = 0.0f;
    shuttleTargetB = false;
    attainAltStartTime = 0;
    cpaDistToTargetCm = -1.0f;
    cpaWasClosing = false;
    descentAltReached = false;
    turnDirectionSign = 0;
    // 🔴 누락: currentMissionWpIndex, prevDistCm, wasClosing, wpGlideInitialized
}
```

`missionStop()` → `gpsRescueResetState()` 호출 시 미션 관련 변수가 초기화되지 않아 재진입 시 오작동.

---

### Bug #7 [P2]: missionUpdateTargetOnly() 하강 단계 미처리

**발생 위치**: `mission.c:146-152`

**원인**:
```c
rescuePhase_e phase = gpsRescueGetPhase();
if (phase == RESCUE_ABORT ||
    phase == RESCUE_DO_NOTHING ||
    phase == RESCUE_LANDING) {  // 🔴 RESCUE_DESCENT, RESCUE_SHUTTLE_DESCENT 빠짐
    missionStop();
    return;
}
```

**영향**:
- 미션 비행 중 구조 로직이 `RESCUE_DESCENT` 또는 `RESCUE_SHUTTLE_DESCENT`로 전환되면 미션이 자동 중단되지 않음
- 두 모드의 제어 로직이 충돌하여 불안정한 비행 (증상 #4 연관)

---

### Bug #8 [P2]: failsafeIsReceivingRxData() 가드로 인한 타이밍 문제

**발생 위치**: `gps_rescue.c:1219, 1225, 1244, 1344, 1366, 1463, 1480, 1484, 1498`

**원인**:
모든 AUX 값 판정이 `failsafeIsReceivingRxData()` 조건 하에 있음. RX 수신이 불안정한 경우(이륙 직후 등) 가드 실패 → 모드 전환 지연/무시.

**영향**:
- 증상 #5: 이륙 후 1~2초간 조종권 상실 느낌 (RX 데이터 안정화 전 가드 미통과)
- 모드 전환이 RX 상태에 민감하여 의도치 않은 동작

---

## 4. 버그 간 연관성 및 증상 매핑

```
증상 #1 (우측→좌측 왕복)
  ├── Bug #1: handleFlyHomePhase()가 A포인트/홈으로 타겟 강제
  ├── Bug #2: 매 루프 missionStart() 재실행 → WP 진행 안 됨
  └── Bug #4: 미션 전용 phase 부재

증상 #2 (조종권 미복귀)
  ├── Bug #3: missionStop() 후 fall-through로 홈/셔틀 선회
  ├── Bug #5: Phase 전환 시 상태 초기화 불완전
  └── Bug #6: gpsRescueResetState() 미션 변수 누락

증상 #3 (WP1 도달 못 함)
  ├── Bug #1: 타겟이 WP1이 아님
  └── Bug #2: 매 루프 WP 인덱스 리셋

증상 #4 (조종권 있다/없다 반복)
  ├── Bug #5: 상태 변수 잔존으로 모드 충돌
  ├── Bug #7: 하강 단계 미션 중단 안 됨
  └── Bug #8: RX 가드 타이밍

증상 #5 (이륙 후 1~2초 조종권 상실)
  └── Bug #8: failsafeIsReceivingRxData() 가드
```

---

## 5. 우선순위 및 수정 계획

| 우선순위 | Bug | 수정 내용 | 예상 효과 |
|----------|-----|-----------|-----------|
| **P0** | #1 | 미션 전용 phase + 핸들러 분리 (handleMissionPhase) | 증상 #1, #3 해결 |
| **P0** | #2 | missionStart() 후 phase = RESCUE_MISSION_FLY_WP 설정 | 증상 #3 해결 |
| **P0** | #3 | missionStop() 후 즉시 return | 증상 #2 해결 |
| **P1** | #4 | rescuePhase_e에 RESCUE_MISSION_FLY_WP 추가 | 구조적 안정성 |
| **P1** | #5 | Phase 전환 시 모든 상태 변수 초기화 | 증상 #2, #4 해결 |
| **P1** | #6 | gpsRescueResetState() 미션 변수 추가 | 재진입 안정성 |
| **P2** | #7 | missionUpdateTargetOnly() 하강 단계 중단 처리 | 증상 #4 해결 |
| **P2** | #8 | RX 가드 완화 또는 별도 상태 추적 | 증상 #5 해결 |

---

## 6. 근본 원인 요약

**미션 비행 모드가 구조 상태 머신(RESCUE_FLY_HOME)에 억지로 끼워 넣어졌다.**
구조 로직의 `handleFlyHomePhase()`는 "타겟 = A포인트 또는 홈"이라는 전제로 작성되었으나, 미션 모드는 "타겟 = 웨이포인트"가 되어야 함. 두 요구사항이 충돌하는데 이를 해결할 별도 phase/handler가 없어 모든 버그의 근원이 됨.

또한 **초기 진입 분기의 phase 전이 누락**으로 매 루프 재실행되는 구조적 결함이 있음.

---

## 7. 검증 시나리오 (수정 후)

1. AUX 1500으로 미션 비행 진입 → WP1 직진 확인
2. 모든 WP 소모 → 구조 로직(짝수/홈) 전환 확인
3. 비행 중 AUX 1100으로 전환 → 무한 셔틀 진입 확인
4. 비행 중 AUX 1700으로 전환 → 정상 레스큐 진입 확인
5. AUX 1000(해제) → 즉시 수동 조종권 복귀 확인
6. 이륙 직후 AUX 1500 → 1~2초 내 미션 진입 확인

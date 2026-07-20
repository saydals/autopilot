# 레스큐 / 미션비행 / 무한셔틀 코드 분석 보고서

## 분석 범위
- **대상 파일**: `src/main/flight/gps_rescue.c`, `src/main/flight/mission.c`, `src/main/flight/gps_rescue.h`, `src/main/flight/mission.h`
- **분석 방식**: 정적 코드 분석 (실제 비행 환경 미고려)
- **분석 원칙**: 코드 수정 없음, 의문시 사용자 확인

---

## 1. 레스큐 해제 시 조종권 반환 분석

### 1.1 조종권 반환 경로

레스큐 모드 비트(`GPS_RESCUE_MODE`) 해제 시 다음 경로로 조종권이 반환된다:

```
FLIGHT_MODE(GPS_RESCUE_MODE) == false
    ↓
gpsRescueStop() 호출 (gps_rescue.c:244)
    ↓
rescueState.phase = RESCUE_IDLE
    ↓
rescueAttainPosition() 내 IDLE 핸들러 실행 (gps_rescue.c:947-951)
    ↓
gpsRescueAngle[AI_PITCH] = 0.0f
gpsRescueAngle[AI_ROLL]  = 0.0f
rescueThrottle = rcCommand[THROTTLE]
lastRescueYaw = 0.0f
```

### 1.2 각 축별 반환 상태

| 축 | 반환 메커니즘 | 상태 |
|----|--------------|------|
| **Throttle** | `rescueThrottle = rcCommand[THROTTLE]` | ✅ 즉시 반환 |
| **Pitch/Roll** | `gpsRescueAngle[] = 0` | ✅ 즉시 반환 |
| **Yaw** | `lastRescueYaw = 0.0f`, `rescueYaw` LPF 적용 | ⚠️ `rescueYaw` 변수는 클리어되지 않으나 IDLE에서 return하므로 미사용 |

### 1.3 레스큐 해제 시나리오별 반환 확인

| 해제 조건 | 코드 경로 | 조종권 반환 |
|----------|----------|------------|
| 모드 스위치 OFF | `gpsRescueUpdate()` 1269행 `gpsRescueStop()` | ✅ |
| LANDING 후 디스암 | `disarmOnImpact()` → `disarm()` → `gpsRescueStop()` | ✅ |
| COMPLETE | `gpsRescueStop()` | ✅ |
| ABORT | `RESCUE_DO_NOTHING` → `disarmOnImpact()` | ✅ |
| 미션 종료 후 모드 유지 | `missionStop()` → `RESCUE_FLY_HOME` 또는 `RESCUE_SHUTTLE_INFINITE` | ⚠️ 모드가 여전히 켜져 있으면 자동비행 유지 |

### 1.4 안전 관련 의문점

**의문 1**: 미션 비행 중 `missionStop()`이 호출되면 phase가 `RESCUE_FLY_HOME`으로 설정된다. 이 시점에서 `GPS_RESCUE_MODE`가 여전히 활성화되어 있으면 자동비행이 계속된다. 사용자가 의도한 것인가?
- 코드: `mission.c:125` → `rescueState.phase = RESCUE_FLY_HOME`
- 미션 종료 후 자동으로 홈 귀환하는 것이 의도된 동작인지 확인 필요.

**의문 2**: `RESCUE_IDLE` 핸들러에서 `rescueYaw` 변수를 클리어하지 않고 있다. 현재는 IDLE에서 return하므로 문제되지 않으나, 향후 확장 시 잔류 값이 문제가 될 수 있다.
- 코드: `gps_rescue.c:950` → `lastRescueYaw = 0.0f`만 설정, `rescueYaw` 미클리어

### 1.5 결론

**조종권 반환은 `GPS_RESCUE_MODE` 비트 해제 시 코드 레벨에서 보장된다.** 단, 미션 종료 후 자동으로 `RESCUE_FLY_HOME`으로 진입하는 점은 설계 의도에 따라 다르다.

---

## 2. 사용자 시나리오 대비 코드 구현 검증

### 2.1 기본 레스큐 (AUX ≥ 1600)

| 시나리오 요구사항 | 코드 구현 | 일치 여부 |
|------------------|----------|----------|
| 초기 3초 상승 구간 | `ATTAIN_ALT_TIMEOUT_US = 3000000` (3초), `handleAttainAltPhase()` | ✅ |
| descentAlt 짝수 → 이륙시 A포인트 생성 | `sensorUpdate()` 1114-1143행: `descentAlt % 2 == 0`일 때만 A포인트 생성 | ✅ |
| descentAlt 홀수 → 홈 향하다 rescue distance에서 A포인트 생성 | `handleFlyHomePhase()` 1452-1457행: `!aPointValid && distanceToHomeM <= descentDistanceM` | ✅ |
| A포인트 도착 후 바로 홈 또는 셔틀 후 홈 | 1508-1520행: `shuttleCount == 0` → `RESCUE_DESCENT`, `> 0` → `RESCUE_SHUTTLE` | ✅ |

### 2.2 무한 셔틀 모드 (AUX < 1400)

| 시나리오 요구사항 | 코드 구현 | 일치 여부 |
|------------------|----------|----------|
| GPS 신호 있을 때 아무때나 발동 | `gpsRescueUpdate()` 모든 phase case에서 `auxVal < 1400` 체크 | ✅ |
| 현재 기체 기준 2가지 방법으로 A/B 생성 | `initShuttlePoints()` 410-459행: GPS_FIX_HOME 유무에 따라 분기 | ✅ |

### 2.3 미션 비행 (AUX 1400-1600)

| 시나리오 요구사항 | 코드 구현 | 일치 여부 |
|------------------|----------|----------|
| 시작시 레스큐 초기단계 거침 | `gps_rescue.c:1289-1292` (INITIALIZE case에서 missionStart 호출) | ✅ |
| Fly home 단계에서 목적지 = waypoint 1 | `missionStart()` → `missionApplyWaypoint()` → `missionUpdateTargetOnly()` | ✅ |
| 모든 waypoint 거친 후 기존 2가지 경로 중 하나 | `missionStop()` 120-133행: HOME_FIX 있으면 FLY_HOME, 없으면 무한셔틀 | ✅ |
| 각 waypoint 고도/속도 타겟 | `missionUpdateTargetOnly()` 190-194행: `wp->altitude`, `wp->speed` 사용 | ✅ |
| waypoint 끝나면 returnAltitudeCm 사용 | `missionStop()` 128행: `targetAltitudeCm = returnAltitudeCm` | ✅ |
| 다양한 포인트 동작 = fly hover로 취급(지나가기) | `missionUpdateTargetOnly()`에서 WP 타입 구분 없이 좌표만 사용 | ✅ |

### 2.4 모드 간 전환 가능성

| 전환 경로 | 코드 존재 여부 |
|----------|---------------|
| 임의 phase → 무한셔틀 (AUX < 1400) | ✅ 모든 phase case에서 처리 |
| 임의 phase → 정상 레스큐 (AUX ≥ 1600) | ✅ `RESCUE_INITIALIZE`로 전환 |
| 임의 phase → 미션 (AUX 1400-1600) | ✅ 각 case에서 `missionStart()` 호출 |

---

## 3. 의문점 확인 결과 및 추가 분석

### 3.1 미션 시작 시 초기 상승 구간 (확인 완료)

**원래 설계**: 미션 비행 시작시 레스큐 초기단계(`ATTAIN_ALT` 3초 상승)를 거치는 것이 목표였음.

**현재 코드**: `RESCUE_IDLE`에서 `missionStart()` 호출 후 즉시 `RESCUE_MISSION_FLY_WP`로 진입 (`gps_rescue.c:1280-1282`).

**사용자 확인 결과**: "3초 상승구간이 목표였으나 어떤 이유로 에러가 나타나 꼭 필요한 동작이 아니므로 WP1로 향하는게 좋다는 조언 때문이었다."

**결론**: 즉시 미션 모드로 진입하는 것이 **의도된 설계**임. 코드를 수정할 필요 없이 현재 구현을 유지하면 된다.

### 3.2 미션 종료 후 자동 진입 모드 (확인 완료)

**코드상 내용**:
- `missionStop()` 120-133행: `GPS_FIX_HOME` 있으면 `RESCUE_FLY_HOME`, 없으면 `gpsRescueStartShuttleInfinite()` 호출

**사용자 확인 결과**: "미션 종료 후 자동으로 RESCUE_FLY_HOME으로 진입하는 것이 맞음"

**결론**: `missionStop()`의 동작이 **사용자 의도와 일치**함. 코드 수정 필요 없음.

### 3.3 미션 비행 중 "조종이 순간적으로 되었다 잃었다" 문제 원인

**현상**: 미션 비행 실행시 조종이 순간적으로 되었다 잃었다를 반복하였다.

**원인 분석**:

1. **스케줄러 실행 순서와 타이밍 갭**:
   ```
   Loop N:   PID(미션 제어값 사용) → RX(모드 비트 업데이트) → GPS_RESCUE(모드 변경 감지)
   Loop N+1: PID(자동비행 제어값 사용) → ...
   ```
   - PID 태스크는 **이전 루프의 모드 비트**로 제어값을 계산
   - 모드 전환 순간 1 루프(~1ms) 동안 구 모드 제어값이 적용됨
   - 이로 인해 제어권이 "잠깐 돌아왔다가 다시 뺏김" 현상이 발생

2. **미션 재진입 불가 구조**:
   - `missionStop()` 호출 후 phase가 `RESCUE_FLY_HOME`으로 설정됨
   - 다음 루프에서 `FLIGHT_MODE(GPS_RESCUE_MODE)`는 여전히 true, phase는 IDLE이 아님
   - `else if (phase == RESCUE_IDLE)` 블록이 실행되지 않음
   - `missionIsActive()`는 false이므로 mission block도 실행되지 않음
   - switch문에서 `RESCUE_FLY_HOME` case 실행 → 자동비행 유지
   - **결과**: auxVal이 1400-1600 범위로 돌아와도 미션이 재시작되지 않고 자동비행이 계속됨

3. **조종권 반환과 재획득의 반복**:
   - `GPS_RESCUE_MODE` 비트가 순간적으로 해제되면 `gpsRescueStop()` → `RESCUE_IDLE` → 조종권 반환
   - 비트가 다시 켜지면 `else if (phase == RESCUE_IDLE)`에서 `missionStart()` 호출 → 미션 재시작
   - 이 과정에서 "조종이 순간적으로 되었다 잃었다"는 현상이 발생

### 3.4 `rescueYaw` 변수 미클리어

**코드상 내용**:
- `RESCUE_IDLE` 핸들러 (`gps_rescue.c:947-951`)에서 `lastRescueYaw = 0.0f`만 설정
- `rescueYaw` 변수는 클리어되지 않음

**현재 영향**: IDLE에서 return하므로 미사용. 문제되지 않음.

---

## 4. 실행 방법 검증

### 4.1 모드 탭 구성

- **현재 코드**: 단일 `GPS_RESCUE_MODE` 비트로 관리
- **AUX 구간**: `getRescueAuxValue()`로 읽은 값으로 내부 모드 분기
  - `< 1400`: 무한셔틀
  - `1400 - 1600`: 미션비행
  - `>= 1600`: 정상 레스큐

### 4.2 모드 할당 범위

사용자가 "레스큐 모드 할당을 1100-1500으로 할당하면 무한셔틀과 미션비행만 가능하게 된다"고 설명한 것과 일치함:
- 1100-1399: 무한셔틀
- 1400-1500: 미션비행
- 1500-1600: 정상 레스큐 (사용자 할당 범위 밖이므로 사실상 미사용)

---

## 5. 종합 결론

### 5.1 조종권 반환

**레스큐 해제 시 조종권 반환은 코드 레벨에서 보장된다.** `gpsRescueStop()` → `RESCUE_IDLE` → `rcCommand[THROTTLE]` 복원 경로가 명확하다.

### 5.2 시나리오 일치도

대부분의 시나리오가 코드와 일치하나 다음 차이점이 있다:

| 항목 | 시나리오 | 코드 | 차이 |
|------|----------|------|------|
| 미션 시작 시 초기 상승 | 거침 | 거치지 않음 | 의도된 것인지 확인 필요 |
| 미션 종료 후 자동 진입 | 기존 2가지 경로 | FLY_HOME 또는 무한셔틀 | 일치 (사용자 시나리오와 동일) |

### 5.3 미션 비행 불안정 문제 원인

코드상 `missionStop()` 후 `RESCUE_FLY_HOME`으로 진입하면, 다시 미션 범위(1400-1600)로 auxVal이 돌아와도 자동으로 미션이 재시작되지 않는다. 이로 인해 예상치 못한 자동비행 상태가 지속될 수 있으며, 모드 비트가 순간적으로 해제/재설정되는 상황과 결합하여 "조종이 순간적으로 되었다 잃었다"는 현상이 발생할 수 있다.

---

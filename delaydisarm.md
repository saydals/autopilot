# Delayed Disarming 구현 계획 및 분석 문서

> 작성일: 2026-07-31
> 대상: Betaflight Autopilot (고정익 비행기용)
> 목적: 코드 수정 없이, ARM 스위치 OFF 시 지연 Disarming 기능의 구현 가능성과 위험을 분석

---

## 1. 배경: 기존 문제

### 현재 동작

```
ARM 스위치 OFF
  → rc_controls.c 에서 DISARM_REASON_SWITCH로 disarm() 호출
  → core.c:427 에서 DISABLE_ARMING_FLAG(ARMED)
  → mixer.c:421~424 에서 !ARMED 확인 → motor_disarmed[] 출력 (모터 0)
  → 고정익 서보는 ARMED와 무관하게 계속 출력
```

### 문제 상황

공중에서 ARM 스위치를 실수로 끄면:
- 모터 즉시 정지
- 자이로/자세 안정화도 중단
- 재아밍 시 gyro가 재초기화되어 공중에서 성공하기 어려움
- 기체는 계속 움직이지만 제어 불가 상태

사용자 경험상 비행 중 아밍키를 실수로 끄는 경우가 잦으며, 이때 기체가 추락하게 됨.

---

## 2. 사용자 요구사항

### 원하는 동작

```
ARM 스위치 OFF
  → 실제 disarm은 아직 하지 않음
  → throttle/motor 출력만 즉시 0
  → ARMED 플래그와 서보 제어 유지
  → 설정 시간(기본 3초) 동안 ARM 스위치 ON 복구 대기

3초 이내 ARM 스위치 ON 복구
  → delayed disarm 취소
  → throttle 출력 정상 복구
  → 기존 ARMED 상태 유지

3초 경과 후 ARM 스위치 미복구
  → 실제 disarm() 호출
  → ARMED 해제
  → 일반 disarm 동작 (블랙박스, 비프, 통계 등)
```

### 사용자 확인 사항

- A포인트 캡처: 수백 번 테스트에서 항상 예상 위치에 생성됨
- WP 전체 속도: Rescue 속도 이상으로 제한하는 설계 의도
- CPA 터치: 정확한 중심점 터치보다 주변 배회를 방지하는 방식이 실제 비행에서 안정적
- 셔틀 하강 ~ 랜딩: 항상 만족스러운 동작
- 자동 착륙: 기대하기 어려우며, 위험 시 Rescue 해제 후 수동 착륙
- 공중 disarm 후 재아밍: 자이로 미안정으로 불가능

---

## 3. 현재 코드 구조 분석

### 3-1. Disarm 경로

**`src/main/fc/rc_controls.c:166~187`** — ARM 스위치 OFF 감지

```c
} else {
    resetTryingToArm();
    resetArmingDisabled();
    const bool boxFailsafeSwitchIsOn = IS_RC_MODE_ACTIVE(BOXFAILSAFE);
    if (ARMING_FLAG(ARMED) && (failsafeIsReceivingRxData() || boxFailsafeSwitchIsOn)) {
        rcDisarmTicks++;
        if (rcDisarmTicks > 3) {
            disarm(DISARM_REASON_SWITCH);
        }
    }
}
```

ARM 스위치 OFF 시 3회 연속 확인 후 `disarm(DISARM_REASON_SWITCH)` 호출.

**`src/main/fc/core.c:427~472`** — `disarm()` 함수

```c
void disarm(flightLogDisarmReason_e reason)
{
    if (ARMING_FLAG(ARMED)) {
        DISABLE_ARMING_FLAG(ARMED);
        lastDisarmTimeUs = micros();
        // ... blackbox, beep, stats 등
    }
}
```

`ARMED` 플래그를 즉시 해제.

**`src/main/fc/core.c:474~619`** — `tryArm()` 함수

ARM 스위치 ON 시 호출. `updateArmingStatus()`에서 모든 arming disable 조건을 확인한 후 `ENABLE_ARMING_FLAG(ARMED)`.

### 3-2. Mixer 출력 경로

**`src/main/flight/mixer.c:751~762`** — Motor 출력 최종 결정

```c
if (featureIsEnabled(FEATURE_MOTOR_STOP)
    && ARMING_FLAG(ARMED)
    && !mixerRuntime.feature3dEnabled
    && !airmodeEnabled
    && !FLIGHT_MODE(GPS_RESCUE_MODE)
    && (rcData[THROTTLE] < rxConfig()->mincheck)) {
    applyMotorStop();
} else {
    applyMixToMotors(motorMix, activeMixer);
}
```

**`src/main/flight/mixer.c:341~345`** — Disarmed 모드 motor 출력

```c
// Disarmed mode
for (int i = 0; i < mixerRuntime.motorCount; i++) {
    motor[i] = motor_disarmed[i];
}
```

**`src/main/flight/mixer.c:420~424`** — Disarmed 모드 motor 출력 (두 번째 위치)

```c
// Disarmed mode
if (!ARMING_FLAG(ARMED)) {
    for (int i = 0; i < mixerRuntime.motorCount; i++) {
        motor[i] = motor_disarmed[i];
    }
}
```

### 3-3. 서보 출력

**`src/main/flight/servos.c:690~692`** — Tricopter만 ARMED로 서보 차단

```c
if (!(servosTricopterIsEnabledServoUnarmed() || ARMING_FLAG(ARMED))) {
    servo[SERVO_RUDDER] = 0; // kill servo signal completely.
}
```

고정익(Flying Wing 등)은 ARMED와 무관하게 서보 계속 출력.

### 3-4. 기존 auto_disarm_delay

**`src/main/fc/rc_controls.h:134`**

```c
uint8_t auto_disarm_delay;  // allow automatically disarming multicopters after auto_disarm_delay seconds of zero throttle
```

**`src/main/fc/core.c:901~925`** — 사용 위치

```c
const timeUs_t autoDisarmDelayUs = armingConfig()->auto_disarm_delay * 1e6;
if (ARMING_FLAG(ARMED)
    && featureIsEnabled(FEATURE_MOTOR_STOP)
    && !isFixedWing()          // ← 고정익에서는 사용 안 됨
    && !featureIsEnabled(FEATURE_3D)
    && !airmodeIsEnabled()
    && !FLIGHT_MODE(GPS_RESCUE_MODE)
) {
    // throttle low 일정 시간 후 자동 disarm
}
```

`auto_disarm_delay`는 멀티콥터 전용이며, `!isFixedWing()` 조건으로 고정익에서는 비활성.

### 3-5. GPS Rescue / Failsafe disarm 경로

- `gps_rescue.c:1241~1248` — `disarmOnImpact()`: 가속도 기반 충돌 감지 시 즉시 disarm
- `failsafe.c:416~417` — GPS Rescue 종료 시 `DISABLE_FLIGHT_MODE(GPS_RESCUE_MODE)`
- `failsafe.c:367~380` — RX 손실 시 Rescue 즉시 종료

보호 목적의 disarm은 즉시 실행되어야 하며, delayed disarm의 대상이 아님.

---

## 4. 구현 계획

### 4-1. 상태 변수 추가

**위치**: `src/main/fc/core.c` (기존 `disarmAt` 근처, line 152)

```c
static bool    delayedDisarmPending = false;
static timeUs_t delayedDisarmExpiry = 0;
static timeUs_t delayedDisarmArmOffTime = 0;  // ARM OFF 시점 기록용
```

### 4-2. ARM 스위치 OFF 감지 — rc_controls.c

**위치**: `src/main/fc/rc_controls.c:166~187`

현재 코드에서 ARM 스위치 OFF 분기(`else` 블록) 내에 다음 로직 추가:

```
ARMING_FLAG(ARMED) && ARM 스위치 OFF 감지
  → delayedDisarmPending = true
  → delayedDisarmExpiry = currentTimeUs + delay * 1e6
  → disarm() 호출하지 않음
  → ARMED 플래그 유지
```

ARM 스위치 ON 복구 감지도 같은 분기에서 처리:

```
ARMING_FLAG(ARMED) == false (delayed pending 상태) && ARM 스위치 ON
  → delayedDisarmPending = false
  → throttle 출력 정상 복구
  → ARMED 유지
```

### 4-3. Throttle/Motor 출력 강제 0 — mixer.c

**위치**: `src/main/flight/mixer.c:751~762`

`applyMixToMotors()` 호출 후 또는 `applyMotorStop()`와 병렬로:

```c
if (delayedDisarmPending) {
    // delayed disarm 중: motor 출력 강제 0
    for (int i = 0; i < mixerRuntime.motorCount; i++) {
        motor[i] = motor_disarmed[i];
    }
}
```

이것은 `applyMixToMotors()` 이후에 적용되어야 최종 출력이 확실히 0이 됨.

또는 `applyMixToMotors()` 호출 자체를 건너뛰고 `motor_disarmed[]`를 직접 설정하는 방식도 가능.

### 4-4. Delayed Disarm 만료 처리 — core.c

**위치**: `src/main/fc/core.c`의 주기 태스크 또는 `updateArmingStatus()` 내

```c
if (delayedDisarmPending && currentTimeUs >= delayedDisarmExpiry) {
    delayedDisarmPending = false;
    disarm(DISARM_REASON_SWITCH);
}
```

### 4-5. CLI 설정 추가

**위치**: `src/main/cli/settings.c`

새 파라미터:
```c
.fixedwingDisarmDelay = 3  // 초, 0=비활성
```

기존 `auto_disarm_delay`와는 별도 설정.

### 4-6. 안전 분기

다른 disarm 경로는 즉시 실행되어야 함:

| Disarm 원인 | 즉시 실행 여부 |
|---|---|
| `DISARM_REASON_SWITCH` (ARM 스위치 OFF) | 지연 가능 |
| `DISARM_REASON_FAILSAFE` | 즉시 |
| `DISARM_REASON_GPS_RESCUE` | 즉시 |
| `DISARM_REASON_CRASH_PROTECTION` | 즉시 |
| `DISARM_REASON_RUNAWAY_TAKEOFF` | 즉시 |
| `DISARM_REASON_THROTTLE_TIMEOUT` | 즉시 |
| `DISARM_REASON_STICKS` | 즉시 |

---

## 5. 상태 전이 다이어그램

```
[정상 비행: ARMED]
    │
    ├─ ARM 스위치 OFF 감지 (rc_controls.c)
    │   └─ delayedDisarmPending = true
    │      delayedDisarmExpiry = now + 3초
    │      (ARMED 유지, throttle 강제 0)
    │
    ├─ ARM 스위치 ON 복구 (3초 이내)
    │   └─ delayedDisarmPending = false
    │      (ARMED 유지, throttle 정상 복구)
    │
    ├─ 3초 경과 (ARM 스위치 미복구)
    │   └─ disarm(DISARM_REASON_SWITCH)
    │      (ARMED 해제, 실제 disarm)
    │
    ├─ Failsafe / GPS Rescue / Crash 등 발생
    │   └─ 즉시 disarm() (delayed 무시)
    │
    └─ RX 손실
        └─ 즉시 disarm 또는 기존 failsafe 정책 (delayed 무시)
```

---

## 6. 예상하지 못한 위험 상황

### 6-1. Pending 중 GPS Rescue 진입
- ARMED 유지 상태이므로 GPS Rescue가 정상 시작 가능
- GPS Rescue가 throttle을 소유하므로 delayed disarm의 throttle 0을 덮어쓸 수 있음
- GPS Rescue 종료 후 delayed disarm이 다시 throttle 0을 강제할 수 있음
- **권장**: GPS Rescue 활성 상태에서는 delayed disarm을 일시 중단하거나 무효화

### 6-2. Pending 중 Failsafe 발생
- Failsafe가 disarm을 호출하면 즉시 실행되어야 함
- **권장**: failsafe 경로의 disarm은 delayed 상태와 무관하게 즉시 실행

### 6-3. Pending 중 RX 신호 손실
- RX가 복구되면 ARM 스위치 상태를 재평가해야 함
- RX 복구 시 ARM 스위치가 OFF이면 pending 유지, ON이면 취소
- **권장**: RX loss 시 pending을 유지하되, RX 복구 시에만 ARM 스위치 상태를 재확인

### 6-4. ARM 스위치 노이즈 (ON/OFF 빠른 반복)
- 스위치 바운스 또는 RC 패킷 노이즈로 인해 여러 번 상태 전환 발생 가능
- **권장**: ARM 스위치 OFF 확인은 최소 2~3개 연속 프레임에서 안정적으로 OFF인 경우에만 pending 시작
- ON 복구도 마찬가지로 안정적 확인 후 취소

### 6-5. Throttle 복구 시 고출력 순간
- ARM 스위치 복구 시 현재 RC throttle 값이 높으면 motor가 즉시 고출력
- **권장**: throttle을 0에서 시작해 점진적으로 RC 값으로 복구 (ramp)하거나, 복구 시에도 throttle low 확인

### 6-6. Disarm 후 재아밍 시도
- 실제 disarm 후에는 `tryArm()`이 다시 실행되어야 함
- 이때 GPS fix, 위성 수, failsafe 상태 등을 다시 확인
- **권장**: delayed disarm이 만료되어 실제 disarm된 후에는 일반 re-arm 절차와 동일하게 동작

### 6-7. Blackbox / 통계 / OSD
- `disarm()` 호출 시 blackbox event, stats, OSD 업데이트가 발생
- delayed disarm에서는 이벤트를 지연 시점에만 기록해야 함
- **권장**: pending 중에는 disarm 이벤트를 기록하지 않음

---

## 7. 검증 방법 (코드 수정 없이)

### 7-1. 시뮬레이터 기반 검증
- SITL (Software In The Loop) 환경에서 ARM 스위치 OFF/ON 시뮬레이션
- mixer 출력이 실제로 0이 되는지 확인
- 서보 출력이 유지되는지 확인
- 3초 후 disarm 호출 여부 확인

### 7-2. GPS Rescue 경로와의 상호작용
- Rescue 비행 중 ARM 스위치 OFF → Rescue가 계속 동작해야 함
- Rescue 완료 후 ARM 스위치 OFF → delayed disarm 시작
- Rescue 중 Failsafe → 즉시 disarm 확인

### 7-3. Failsafe 시나리오
- RX 손실 발생 시 delayed disarm이 즉시 취소되고 failsafe 동작 확인
- RX 복구 후 ARM 스위치 상태 재확인

### 7-4. 경계 조건
- ARM 스위치 OFF 후 2.9초에 ON 복구 → delayed 취소, 비행 계속
- ARM 스위치 OFF 후 3.1초 경과 → 실제 disarm
- ARM 스위치 OFF 후 0.1초에 ON 복구 → 즉시 취소

---

## 8. 구현 우선순위

1. **핵심 로직**: rc_controls.c에서 ARM OFF 감지 → delayed 상태 전이
2. **출력 제어**: mixer.c에서 delayed 상태 시 motor 출력 강제 0
3. **타이머 관리**: core.c에서 delayed disarm 만료 처리
4. **복구 처리**: rc_controls.c에서 ARM ON 복구 시 pending 취소
5. **안전 분기**: failsafe/GPS Rescue/crash 시 즉시 disarm 보장
6. **CLI 설정**: delay duration 파라미터 추가
7. **검증**: SITL 시뮬레이션으로 각 경로 확인

---

## 9. 요약

코드 수정 없이 현재 구조를 분석한 결과, delayed disarming은 **구현 가능**합니다.

핵심은:
- `disarm()` 자체를 지연시키는 것이 아니라, **ARM 스위치 OFF 경로에서만 pending 상태를 추가**
- `ARMED` 플래그를 3초간 유지하여 gyro/PID/서보 상태 보존
- `mixer.c`에서 pending 상태를 인식해 **최종 motor 출력만 강제 0**
- GPS Rescue, failsafe, crash 등 보호 목적 disarm은 즉시 실행

가장 큰 위험은:
1. pending 중 throttle 복구 시 고출력 순간
2. GPS Rescue와의 throttle 소유권 충돌
3. ARM 스위치 노이즈로 인한 잘못된 pending 시작/취소

이 세 가지를 어떻게 처리하느냐가 구현의 핵심입니다.

---

## 10. 참고: 기존 Rescue Mission 버그 분석 요약

이 문서의 주된 내용은 delayed disarming 분석이지만, 이전 대화에서 분석된 Rescue Mission 관련 사항을 간략히 정리:

- GPS Rescue 미션 모드의 AUX rising-edge 의존성 → 수정 완료
- `missionCheckAdvance()` 미호출 → 수정 완료
- SHUTTLE의 AUX 1400~1600 구간 무한 rollback → 수정 완료
- 미션 완료 후 셔틀 파라미터 사용 불가 → 조건부 위험
- WP 타입 기능 미구현 → 사용자 의도에 따라 FLYOVER로만 사용 중
- A포인트 캡처 범위 20~100m 하드코딩 → 현재 설정에서는 문제 없음
- CPA 터치 방식 → 사용자 테스트에서 안정적 확인됨
- 셔틀 하강 ~ 랜딩 → 사용자 테스트에서 항상 만족

이 항목들은 현재 사용 환경에서 실제 비행 오류로 확인되지 않았습니다.
</arg_value>
</tool_call>
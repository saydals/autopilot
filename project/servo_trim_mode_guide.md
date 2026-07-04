# Servo Trim Mode 사용 설명서

## 개요

Servo Trim Mode는 조종기 트림 버튼으로는 변경할 수 없는 **서보의 물리적 중립값(PWM middle)** 을, Betaflight Configurator의 AUX 스위치와 조종 스틱만을 이용하여 직접 조정할 수 있게 해주는 기능입니다.

Mode를 활성화 시에는 기체는 자동 안정화 자세를 취합니다. ( 쓰로틀 조종가능 피치 러더는 자동 중립 에일러론은 Mode 진입할때의 상태 유지 ) , 즉 기체 조종이 안되므로 주의 해야함.

아밍과 상관없이 작동하며 비행중에서도 작동하지만 익숙하지 않다면 지상에서 조정하세요.

대부분의 비행기는 중립값을 맞추지 않아도 잘 날지만 3D 비행가 같은 롤 회전이 많은 기체의 경우

물리적 중립값이 맞지 않으면 롤링시 꿀렁임이 발생합니다. 에일러론은 맞출 필요가 없으며 피치와 러더를 맞춰야 합니다. 피치는 주로 무게중심과 관련이 있고 러더는 프로펠러에 의한 후류 현상 보정때문에 물리적 중립값이 타면 중립과 다릅니다.

---

## 모드 박스 할당 방법

1. **Betaflight Configurator**를 실행합니다.
2. **Modes** 탭으로 이동합니다.
3. **"SERVO TRIM"** 이라는 이름의 모드를 찾습니다 (permanentId=6, 기존 HEADFREE 박스 자리를 재사용).
4. 원하는 AUX 채널 스위치를 할당합니다.

---

## CLI 설정 파라미터

CLI에서 `get` trim 명령을 내리면 관련 파라메터를 볼수 있습니다. 

```javascript
# get trim_aileron
# trim_aileron = NORMAL
# Allowed range: OFF, NORMAL, REVERSED
```

```javascript
# get servo_trim_step
# servo_trim_step = 5
# Allowed range: 1 - 20
```

- __lookup 타입__(`trim_aileron` 등): `Allowed range: OFF, NORMAL, REVERSED`
- __숫자 범위 타입__(`servo_trim_step` 등): `Allowed range: 1 - 20`

`get` 명령만으로도 어떤 값들을 넣을 수 있는지 바로 확인 가능합니다.

### 파라미터 설정 예시

```
# CLI 접속 후
set servo_trim_step = 3
set trim_aileron = NORMAL
set trim_elevator = NORMAL
set trim_rudder = OFF
save
```

---

## 사용 방법

### 1. 모드 활성화

1. 비행 중 또는 지상에서 AUX 스위치를 **ON**으로 전환합니다.
2. OSD에 **"TRIM"** 문구가 표시됩니다 (Configurator Modes 탭에서도 초록불 확인 가능).
3. 모드 활성 시:
   - **쓰로틀**: 정상 작동 (영향 없음)
   - **에일러론**: 모드 진입 시점의 스틱 위치 값으로 고정
   - **엘리베이터/러더**: 0으로 고정 (스틱 입력 비활성화)

### 2. 트림 조정

1. 조정할 축의 스틱을 **끝점**까지 움직입니다:
   - **증가(+)** : 스틱을 1750 이상으로 이동
   - **감소(-)** : 스틱을 1250 이하로 이동
2. 한 번 움직일 때마다 `servo_trim_step` 값만큼 해당 서보들의 `middle`이 변경됩니다.
3. 변경 시 **Beeper**가 한 번 울립니다 (RX_SET).
4. 같은 끝점에 계속 머물러도 **한 번만** 적용됩니다 (래치/히스테리시스).
5. 다음 스텝을 적용하려면 스틱을 **중립 근처**(1600 미만 / 1400 초과)로 복귀한 후 다시 끝점으로 움직입니다.

### 3. 모드 비활성화

1. AUX 스위치를 **OFF**로 전환합니다.
2. 즉시 정상 조종 상태로 복귀합니다.
3. 변경된 서보 `middle` 값은 **유지**된 상태로 비행이 계속됩니다.

### 4. 변경사항 영구 저장

트림 조정으로 변경된 `middle` 값은 **RAM에만** 저장됩니다. 재부팅 후에도 유지하려면:

```
# CLI 접속
servo    ← 변경된 middle 값 확인
save     ← EEPROM에 영구 저장
```

또는 Configurator에서 모드에서 서보 설정으로 들어가 **Save** 버튼을 클릭합니다.

---

## 트림 방향 자동 계산

트림 방향은 다음 **3요소**를 곱하여 자동으로 계산됩니다:

```
effectiveDir = servoDirection × sign(rule.rate) × sign(servoParams.rate)
```

- **servoDirection()**: `reversedSources` 비트필드에 저장된 반전 설정
- **rule.rate 부호**: 믹서 규칙의 rate 값 부호 (±)
- **servoParams.rate 부호**: 서보 설정의 rate 값 부호 (±)

### 방향이 반대일 때

자동 계산 결과가 실제 타면 방향과 반대라면, CLI에서 해당 축의 `trim_*` 파라미터를 `REVERSED`로 변경하면 즉시 반전됩니다. 

---

## 안전장치

### 1. 트림 한계값 (하드 리밋)

- 모드 진입 시점의 각 서보 `middle` 값을 기준으로 **±100 PWM** 범위를 넘을 수 없습니다.
- 모드를 껐다가 다시 켜면(재진입) 새로운 기준점이 설정됩니다.
- 이 한계는 **비행 안전**을 위한 하드 리밋으로, 연산 오류 방지가 목적이 아닙니다.
- 만약 현장에서 100 이상의 조정이 필요하면 한번 저장후 다시 모드로 진입하면 됩니다. 저장값이 다음 실행시의 중립값으로 정해져 추가로 다시 100 범위까지 조정 가능합니다.

### 2. ANGLE / HORIZON 모드 가드

`ANGLE` 또는 `HORIZON` 비행 모드가 활성화된 상태에서는 Servo Trim Mode가 작동하지 않습니다. ACRO 모드에서만 사용 가능합니다.

### 3. Board Align 모드와 상호배타

`Board Align`(BOXUSER2) 모드가 활성화된 상태에서는 Servo Trim Mode가 동작하지 않으며, 반대도 마찬가지입니다. 두 모드는 동시에 사용할 수 없습니다.

### 4. forwardFromChannel 서보 자동 제외

`forwardFromChannel`이 설정된 서보(조종기 채널을 서보 출력으로 직접 전달)는 트림 대상에서 자동으로 제외됩니다. 이 서보들은 조종기 자체 트림 기능으로 중립을 조정해야 합니다.

---

## Flying Wing (플라잉윙) 특이사항

플라잉윙에서는 두 개의 플래퍼론 서보가 **ROLL** 규칙과 **PITCH** 규칙을 동시에 받습니다. 이 경우:

- **에일러론 트림**: 두 플래퍼론 서보의 middle을 반대 방향으로 조정
- **엘리베이터 트림**: 두 플래퍼론 서보의 middle을 같은 방향으로 조정
- **동시 적용 시**: 두 트림 값이 같은 서보에 **누적(가산)** 됩니다.
  - 예: 왼쪽 서보 middle += (+step) + (+step) = +2step (에일러론 up + 엘리베이터 up)
  - 예: 오른쪽 서보 middle += (-step) + (+step) = 0 (에일러론 down + 엘리베이터 up)

이는 **엘리본(Elevon)** 방식의 의도된 동작입니다.

---

## OSD 표시

- Servo Trim Mode 활성 시 OSD에 **"TRIM"** 문구가 표시됩니다 (FLIGHT MODE 표시 영역).
- ANGLE, HORIZON, AIR 등 다른 모드 표시와 함께 나타납니다.

---

## 문제 해결

### 트림이 전혀 작동하지 않음

1. `servo_trim_step`이 0이 아닌지 확인 (`get servo_trim_step`)
2. 해당 축의 `trim_*` 값이 `OFF`(0)가 아닌지 확인
3. ANGLE/HORIZON 모드가 꺼져 있는지 확인
4. Board Align(BOXUSER2) 모드가 꺼져 있는지 확인
5. `smix dump` 결과에서 해당 축의 믹서 규칙이 `INPUT_STABILIZED_ROLL`/`PITCH`/`YAW` 또는 `INPUT_RC_ROLL`/`PITCH`/`YAW`를 사용하는지 확인

### 트림 방향이 반대

```
set trim_aileron = REVERSED    (또는 elevator/rudder)
```

### 변경한 값이 재부팅 후 사라짐

`save` 명령을 실행하지 않았기 때문입니다. CLI에서 `save`를 실행하거나 Configurator에서 저장 버튼을 클릭하세요.

### Bird Flap 모드와 충돌

Bird Flap(BOXUSER1)이 `INPUT_BIRD_FLAP` 입력 소스를 사용하는 서보는 Servo Trim Mode의 대상이 아닙니다. `smix dump`로 확인하세요. Bird Flap 서보가 아닌 별도의 에일러론/플래퍼론 서보가 있다면 문제없이 동작합니다.

---

## 구현 상세 (개발자 참고)

### 아키텍처

```
AUX Switch ON
    → IS_RC_MODE_ACTIVE(BOXHEADFREE) = true
    → rc.c의 updateRcCommands()에서 Servo Trim 블록 실행
        → rcCommand[ROLL/PITCH/YAW] 고정 (스틱 인터셉트)
        → currentServoMixer[] 순회하여 대상 서보 탐색
        → servoParams(target)->middle 증감
```

### 코어 로직 위치

- **모드 진입/해제 및 스틱 인터셉트**: `src/main/fc/rc.c` — `updateRcCommands()` 함수 내 Servo Trim 블록
- **CLI 파라미터**: `src/main/cli/settings.c` — `servo_trim_step`, `trim_aileron`, `trim_elevator`, `trim_rudder`
- **접근자 함수**: `src/main/flight/servos.c` — `getActiveServoRuleCount()`, `getCurrentServoMixer()`
- **모드 박스**: `src/main/msp/msp_box.c` — BOXHEADFREE 재정의, `getBoxIdState()` 특별 분기

### 관련 파일

| 파일                            | 역할                             |
| ----------------------------- | ------------------------------ |
| `src/main/fc/rc.c`            | Servo Trim Mode 메인 로직, 스틱 인터셉트 |
| `src/main/flight/servos.h`    | 설정 구조체, 상수, 함수 선언              |
| `src/main/flight/servos.c`    | 접근자 함수 구현                      |
| `src/main/cli/settings.h`     | `TABLE_TRIM_DIRECTION` enum    |
| `src/main/cli/settings.c`     | CLI 파라미터 등록                    |
| `src/main/msp/msp_box.c`      | Configurator 모드 박스 정의          |
| `src/main/fc/core.c`          | HEADFREE_MODE 플래그 제거           |
| `src/main/osd/osd_elements.c` | OSD "TRIM" 표시                  |

## 부록: 트림모드 없이 물리 중립 잡는 법 (forwardFromChannel 트릭)

이번 트림모드와는 별개로 존재하는 기존 기법이다.

- 원하는 조종 채널(예: 에일러론)을 남는 AUX 채널(예: AUX3)에 복사하도록 조종기를 설정한다.
- 서보의 `forwardFromChannel`을 그 AUX 채널로 지정한다.
- 원래 채널(에일러론)에 할당되어 있던 조종기 트림 버튼 기능은 삭제하고, 복사된 AUX 채널 쪽에 트림 버튼을 재할당한다.
- 이렇게 하면 조종기 자체 트림 버튼으로 서보의 물리 중립(PWM)을 직접 바꿀 수 있다.

**제약**: AUX 채널이 여유 있어야 하고, 조종기가 트림 버튼의 삭제/재할당을 지원해야 한다. 일부 조종기 기종은 지원하지 않아 모두가 쓸 수 있는 방법은 아니다. 이번에 만드는 Servo Trim Mode는 이 제약 없이 모든 조종기에서 쓸 수 있게 하는 것이 목적이다.

`forwardFromChannel`이 설정된 서보는 Servo Trim Mode 대상에서 자동 제외되므로, 이 두 방식은 서로 간섭하지 않는다.

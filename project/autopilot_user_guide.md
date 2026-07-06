# Autopilot (Waypoint Mission) 사용자 가이드

> **적용 펌웨어**: my-betaflight autopilot 브랜치 (커밋 8a3bf7e+)
> **필요 Configurator**: betaflight.github.io (최신 버전, FlightPlan 탭 지원)

---

적 : 최신 버전의 공식 베타플라이트에서 autopilot 기능을 만들어 컨피규레이터에서 waypoint를 입력 가능하게 하였다. https://master.app.betaflight.com/# 으로 실행하면 최신 버전의 앱이 실행된다.

안드로이드의 경우 최신 베타버전 APK를 설치하는것을 추천한다. 웹버전은 FC와 연결이 잘 안된다.

나는 개인적으로 베타플라이트 구버전 4.5.3을 자체 발전시켜 멀티콥터를 배제한 윙 전용 펌웨어를 개발해

사용중이다.  최신 버전에도 없는 윙 레스큐를 개발해서 ( 대신 멀티콥터 레스큐는 사망) 아주 잘 사용중이다.

최신 버전의 멀티콥터용 autopilot 기능을 도입하려는 계획을 세웠다. 완전 도입은 아니고 최소 코드 수정으로 거의 비슷한 기능을 구현하는것이다. 내가 사용하는 레스큐는 발동지점에서 홈으로 바로 가는것이 아니고 A B 두 포인트를 거쳐서 홈으로 가는 로직이 있다.  최선 컨피규레이터에서 waypoint를 입력 받아 그 좌표를 모두 돌아서 온다면 ( A B 포인트 대신 ) 거의 비슷한 기능을 한다고 본다.

심지어 최신 버전은 윙에 대한 고려는 전혀없지만 내가 개발한 버전은 윙을 위한 다양한 옵션이 있기 때문에 큰 의미가 있다.

최신 버전의 컨피규레이터에서 flight plan 탭을 열어 비행기가 경유할 포인트를 받아온다.

각 포인트에는 위치 고도 속도 그 이외의 것이 존재하지만 위치 고도 속도만 사용한다.

1번에 설정된 위치 고도 속도는 즉시 레스큐 로직에서 타겟 목표로 설정한다. 

다만 고도를 조정하기 위한 상승률 하강률은 기존 레스큐로 제한된다.

속도 역시 기존 레스큐로 제한된다 . 레스큐에 사용하는 목표 속도가 최저 속도가 되며 쓰로들 역시

레스큐에서 사용하는 최대 최소값을 그대로 사용한다. 목표 속도는 컨피규레이터에서 받아와서 타겟으로 

삼지만 레스큐 로직에서 사용되는 여러가지 제한값을 적용 받는다.

GPS 가 있다면 flight plan은 계속 진행되며 종료시 레스큐로 넘긴다. 즉 홈 포인트로 이동한다.

GPS가 없으면 역시 종료하고 레스큐 로직으로 넘긴다. 한번 종료되면 자동으로 flight plan복귀는 안된다.

flight plan은 RX 신호가 붙었을때 발동되며 신호을 잃어도 flight plan은 유지 한다.

waypoint가 없을때 사용자가 aux 키로 Autopilot을 발동시키면 레스큐를 실행한다.

레스큐로 할당된 Aux키값 1000~1400 미만은 무한셔틀 모드  1400이상 1600미만은 Autopilot 을 실행하고 1600 값 이상은 레스큐로 실행한다.

어떤 비행 상태이던지 Autopilot이 실행되면 1번 point로 목적지를 고정해 비행하며 비행을 마치면 레스큐 첫 시작 단계부터 과정을 밟아 레스큐 규칙대로 실행된다.

Autopilot 어느 단계에서든지 Aux키 명령으로 다른 단계 ( 무한셔틀 또는 레스큐 )로 넘어갈수 있다.

상태 변화시 각 상태의 첫 단계부터 실행한다.

## 1. 개요

본 기능은 Betaflight Configurator의 **FlightPlan 탭**에서 입력한 Waypoint를 따라 비행기가 자동으로 경유하는 **Autopilot (Waypoint Mission)** 기능입니다.

기존 GPS Rescue 시스템의 제어 로직을 그대로 사용하므로, Rescue가 정상 동작하는 기체에서만 사용 가능합니다.

### 주요 특징

- 최대 **15개** Waypoint 설정 가능
- Configurator FlightPlan 탭에서 Waypoint 입력/편집
- Waypoint 완료 후 자동 Home 귀환 (Rescue 모드 전환)
- 3가지 모드를 AUX 스위치 하나로 전환

---

## 2. 필요 조건

| 항목     | 필수                           |
| -------- | ------------------------------ |
| GPS 모듈 | ✅ 필수 (GPS Fix 필요)          |
| 기압계   | 권장 (고도 제어 정확도 향상)   |
| 자력계   | 선택 (Yaw 방향 정확도 향상)    |
| AUX 채널 | ✅ 최소 1개 (3-way 스위치 권장) |

---

## 3. AUX 스위치 설정

### 3-Way 스위치 구성

하나의 AUX 채널로 3가지 모드를 전환합니다:

| AUX 값        | 모드               | 설명                    |
| ------------- | ------------------ | ----------------------- |
| `< 1400`      | **셔틀 (Shuttle)** | A-B 포인트 무한 왕복    |
| `1400 ~ 1600` | **Autopilot**      | Waypoint Mission 실행   |
| `> 1600`      | **Rescue**         | Home 귀환 (기본 Rescue) |

### 설정 방법

1. Configurator → **Modes** 탭
2. **GPS RESCUE** 모드를 원하는 AUX 채널에 할당
3. 3-way 스위치를 사용하여 각 모드 전환

> 💡 **팁**: 송신기에서 AUX 채널을 3단 스위치로 설정하고, 각 단의 출력값을 1000/1500/2000 으로 조정하세요.

---

## 4. FlightPlan 탭 사용법

### 4.1 탭 활성화

펌웨어에 Autopilot 기능이 포함된 경우, Configurator 상단에 **FlightPlan** 탭이 자동으로 나타납니다.

### 4.2 Waypoint 입력

1. **FlightPlan** 탭 클릭

2. 지도에서 원하는 지점을 클릭하여 Waypoint 추가

3. 각 Waypoint의 **고도**와 **속도** 설정

4. Waypoint **유형(Type)** 선택 (8가지 전체 저장 가능):

   - `FLYOVER`: 경유 후 다음 목적지로 이동 (기본)
   - `FLYBY`: 경유하되 선회 반경 설정
   - `HOLD`: 지정된 패턴으로 선회
   - `LAND`: 해당 좌표로 착륙
   - `TAKEOFF`: 이륙 절차
   - `ALT_CHANGE`: 고도 변경
   - `DELAY`: 지정 시간 대기
   - `YAW_RATE`: 회전율 설정

   > ⚠️ **현재 구현**: 위 8가지 타입 모두 저장은 가능하나, 모든 타입의 **실제 비행 동작은 `FLYOVER`와 동일**합니다. (위치/고도/속도 위주 비행)

5. **저장(Save)** 버튼 클릭 → 자동으로 CLI 명령 실행

> 💡 **단위 변환 참고**: Configurator UI는 ft/knots/minutes 단위로 표시하지만, 펌웨어 내부는 cm/cm-s/deciseconds로 처리합니다.  
> 예: UI 400ft → `altitude: 12192` (cm), UI 10knots → `speed: 514` (cm/s), UI 0min → `duration: 0` (deciseconds)

> 💡 Waypoint는 `save` 명령 또는 FlightPlan Save 버튼을 통해 Flash(EEPROM)에 저장되며, 재부팅 후에도 유지됩니다.

### 4.3 CLI 명령 (고급 사용자)

Configurator 대신 CLI에서 직접 Waypoint를 관리할 수 있습니다:

```bash
# Waypoint 목록 보기
waypoint list

# Waypoint 삽입 (8개 인자)
waypoint insert 0 37.1234567 127.1234567 12192 514 FLYOVER 0 ORBIT

# 모든 Waypoint 삭제
waypoint clear

# 설정 저장
save
```

**인자 설명**:

| 순서 | 항목     | 단위        | 예시             |
| ---- | -------- | ----------- | ---------------- |
| 0    | 인덱스   | -           | 0                |
| 1    | 위도     | 도 (float)  | 37.1234567       |
| 2    | 경도     | 도 (float)  | 127.1234567      |
| 3    | 고도     | cm          | 12192            |
| 4    | 속도     | cm/s        | 514              |
| 5    | 유형     | 문자열      | FLYOVER          |
| 6    | 지속시간 | deciseconds | 0 (HOLD/DELAY용) |
| 7    | 패턴     | 문자열      | ORBIT            |

---

## 5. 비행 절차

### 5.1 이륙 전 확인사항

- [ ] GPS 3D Fix (6개 이상 위성 권장)
- [ ] Home 포인트 설정 완료
- [ ] AUX 스위치 중립(Rescue 모드 off) 확인
- [ ] FlightPlan에 Waypoint 정상 로드 확인

### 5.2 Autopilot 실행

1. 수동 이륙 후 안전한 고도 도달
2. AUX 스위치를 **Autopilot** 위치(1400~1600)로 전환
3. 비행기가 자동으로 Waypoint #1 방향으로 선회
4. 각 Waypoint를 순차적으로 경유
5. 모든 Waypoint 완료 후 자동으로 Home 귀환 (Rescue 모드)

### 5.3 도중 전환

하강/착륙 중이 아니라면 AUX 스위치로 셔틀 ↔ Autopilot ↔ Rescue 자유롭게 전환할 수 있습니다.

---

## 5.4 CLI 명령어

CLI에서 Waypoint를 직접 관리할 수 있습니다.

### waypoint list

등록된 모든 Waypoint를 **Configurator 호환 형식**으로 출력합니다.

```
# Waypoint count: 2
waypoint insert 0 37.1234567 127.1234567 12192 514 FLYOVER 0 ORBIT
waypoint insert 1 37.2345678 127.2345678 300 15 FLYBY 2 FIGURE8
```

### waypoint insert

새 Waypoint를 추가합니다. 인덱스는 0부터 시작합니다.

```
waypoint insert <인덱스> <위도> <경도> <고도_cm> <속도_cm/s> <타입> <지속시간_ds> <패턴>
```

| 인자     | 예시          | 단위        | 설명                                                                                    |
| -------- | ------------- | ----------- | --------------------------------------------------------------------------------------- |
| 인덱스   | `0`           | -           | 0부터 14까지 (15개 제한)                                                                |
| 위도     | `37.1234567`  | 도 (float)  | 소수점 7자리                                                                            |
| 경도     | `127.1234567` | 도 (float)  | 소수점 7자리                                                                            |
| 고도     | `12192`       | cm          | 정수 (400ft = 12192cm)                                                                  |
| 속도     | `514`         | cm/s        | 정수 (10knots = 514cm/s, YAW_RATE일 때는 deg/s)                                         |
| 타입     | `FLYOVER`     | 문자열      | `FLYOVER` / `FLYBY` / `HOLD` / `LAND` / `TAKEOFF` / `ALT_CHANGE` / `DELAY` / `YAW_RATE` |
| 지속시간 | `0`           | deciseconds | `HOLD`/`DELAY` 타입용 (30초 = 300ds)                                                    |
| 패턴     | `ORBIT`       | 문자열      | `ORBIT` / `FIGURE8`                                                                     |

### waypoint update

기존 Waypoint를 수정합니다. insert와 동일한 인자를 사용합니다.

```
waypoint update <인덱스> <위도> <경도> <고도_cm> <속도_cm/s> <타입> <지속시간_ds> <패턴>
```

### waypoint remove

지정 인덱스의 Waypoint를 삭제합니다.

```
waypoint remove <인덱스>
```

### waypoint clear

모든 Waypoint를 삭제합니다.

```
waypoint clear
```

### waypoint status

현재 Waypoint 개수와 진행 상태를 확인합니다.

```
waypoint status
```

### waypoint list

등록된 모든 Waypoint를 출력합니다.

```
waypoint list
```

### dump / diff

dump에서는 표시되지 않음. 

---

## 6. 비행 중 표시 (OSD)

OSD에 현재 모드와 진행 상태가 나타납니다:

### Ready Mode (OSD_READY_MODE 요소)

| 표시       | 의미                             |
| ---------- | -------------------------------- |
| `AUTO-1/4` | Waypoint 미션 진행 중 (현재 1/4) |
| `FLY HOME` | Rescue - Home 귀환 중            |
| `CLIMB`    | Rescue - 상승 중                 |
| `DESCEND`  | Rescue - 하강 중                 |
| `SHUT-001` | 셔틀 모드 (왕복 횟수)            |

### GPS 좌표 라벨 (GPS 좌표 필드 접두어)

| 표시      | 의미                      |
| --------- | ------------------------- |
| `W`       | Waypoint 방향으로 비행 중 |
| `A` / `B` | 셔틀 A/B 포인트 방향      |
| `H`       | Home 귀환 중              |

---

## 7. Pre-arm Wiggle (이륙 전 서보 알림)

비행기가 이륙 가능한 상태가 되면 서보가 주기적으로 움직여 조종사에게 알려줍니다.
**`ready_to_arm_wiggle_hz`** CLI 설정으로 켜고 끌 수 있습니다 (0=OFF, 1~6Hz).

### 위글 패턴 의미

| 위글                                                   | 의미                                        |
| ------------------------------------------------------ | ------------------------------------------- |
| **🟡 에일러론**만 위글                                  | **아밍 준비 완료** (기본)                   |
| **🟢 에일러론 + 엘리베이터** 동시 위글                  | **+ GPS OK** (3D Fix + 위성 `minSats` 이상) |
| 🔵 **에일러론 위글** → 1초 후 **엘리베이터 단독** 위글 | **+ Waypoint 존재** (모든 준비 완료)        |

### 패턴별 동작

| 조건                       | Phase 1 (1초)         | Phase 2 (1초)   | 의미                      |
| -------------------------- | --------------------- | --------------- | ------------------------- |
| 아밍 준비만                | 에일러론              | -               | 기본 Rescue 준비          |
| + GPS Fix & `minSats` 충족 | 에일러론 + 엘리베이터 | -               | Rescue 가능               |
| + GPS & Waypoint 존재      | 에일러론 + 엘리베이터 | 엘리베이터 단독 | **Autopilot 준비 완료** 🚀 |

> 💡 GPS 조건은 CLI `gps_rescue_min_sats` (기본 8개)로 조정 가능합니다.  
> 💡 Waypoint만 있고 GPS 조건 미달이면 Phase 1은 에일러론만 위글, Phase 2에서 엘리베이터가 위글합니다.  
> 💡 조종간을 움직이면 위글이 즉시 중단되고 10초간 재개되지 않습니다.

---

## 8. 안전 기능

### 7.1 Waypoint 타임아웃

- 한 Waypoint에 **5분 이상** 머무르면 자동으로 다음 Waypoint로 Skip
- 강한 바람이나 GPS drift로 인한 정체 상황 방지

### 7.2 Home 미설정 시

- Home 포인트가 없으면 모든 Waypoint 완료 후 **현재 위치 기준 헤딩 기반 무한셔틀 모드**로 전환 (A-B 포인트 무한 왕복)
- Home 포인트가 있으면 자동 **Home 귀환**

### 7.3 GPS 손실 시

- 기존 Rescue의 `performSanityChecks()`가 처리
- GPS 손실 감지 시 Rescue 절차에 따라 안전 대응

### 7.4 무장(ARM) 상태 보호

- 비행 중(ARMED)에는 CLI `waypoint insert/clear/update/remove` 명령이 차단됨
- `waypoint list` 및 `waypoint status` 조회만 가능

---

## 8. 주의사항

⚠️ **고도 관리**: Waypoint에 설정된 고도는 목표값이며, 기체의 상승/하강 성능에 따라 실제 도달 시간이 다를 수 있습니다.

⚠️ **속도 제한**: Waypoint 속도보다 Rescue에 설정된 최소 속도(`groundSpeedCmS`)가 더 크면 Rescue 속도가 우선 적용됩니다 (실속 방지).

⚠️ **저장**: Waypoint는 `save` 명령 시 Flash에 저장되어 재부팅 후에도 유지됩니다. 단, `save` 없이 전원을 끄면 소멸됩니다.

⚠️ **Configurator 버전**: 반드시 최신 Betaflight Configurator (master.app.betaflight.com)를 사용하세요. 구버전 Configurator에는 FlightPlan 탭이 없습니다.

---

## 9. 문제 해결

| 증상                  | 원인                   | 조치                                    |
| --------------------- | ---------------------- | --------------------------------------- |
| FlightPlan 탭 안 보임 | Configurator 버전 문제 | master.app.betaflight.com 사용          |
| Waypoint가 저장 안 됨 | RAM-only 특성          | Configurator에서 저장 후 `save`         |
| Autopilot 실행 안 됨  | Waypoint 없음          | Waypoint 추가 후 재시도                 |
| AUX 전환이 안 됨      | Range 보정 문제        | 송신기 AUX 출력값 확인 (1000/1500/2000) |
| 비행기가 빙글빙글 돔  | GPS Course 불안정      | GPS 위성 수 증가 또는 자력계 보정       |

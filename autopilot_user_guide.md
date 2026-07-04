# Autopilot (Waypoint Mission) 사용자 가이드

> **적용 펌웨어**: my-betaflight autopilot 브랜치  
> **필요 Configurator**: betaflight.github.io (최신 버전)

---

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
3. 각 Waypoint의 **고도(ft)**와 **속도(knots)** 설정
4. Waypoint **유형(Type)** 선택: ( 현재 Flyover만 구현됨 )
   - `FLYOVER`: 경유 후 다음 목적지로 이동 (기본)
   - `FLYBY`: 경유하되 선회 반경 설정
   - `HOLD`: 지정된 패턴으로 선회
   - `LAND`: 해당 좌표로 착륙
5. **저장(Save)** 버튼 클릭 → 자동으로 CLI 명령 실행

> ⚠️ **참고**: Waypoint는 RAM에만 저장됩니다. 전원을 끄면 사라지므로, 비행 전 반드시 Configurator에서 다시 업로드하세요.

### 4.3 CLI 명령 (고급 사용자)

Configurator 대신 CLI에서 직접 Waypoint를 관리할 수 있습니다: ( 현재 램에만 저장하기때문에 불가능 )

```bash
# Waypoint 목록 보기
waypoint list

# Waypoint 삽입 (8개 인자)
waypoint insert 0 37.1234567 127.1234567 400 10 FLYOVER 0 ORBIT

# 모든 Waypoint 삭제
waypoint clear

# 설정 저장
save
```

**인자 설명**:

| 순서 | 항목     | 단위       | 예시             |
| ---- | -------- | ---------- | ---------------- |
| 0    | 인덱스   | -          | 0                |
| 1    | 위도     | 도 (float) | 37.1234567       |
| 2    | 경도     | 도 (float) | 127.1234567      |
| 3    | 고도     | feet       | 400              |
| 4    | 속도     | knots      | 10               |
| 5    | 유형     | 문자열     | FLYOVER          |
| 6    | 지속시간 | 분         | 0 (HOLD/DELAY용) |
| 7    | 패턴     | 문자열     | ORBIT            |

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
waypoint insert 0 37.1234567 127.1234567 400 10 FLYOVER 0 ORBIT
waypoint insert 1 37.2345678 127.2345678 300 15 FLYBY 2 FIGURE8
```

### waypoint insert
새 Waypoint를 추가합니다. 인덱스는 0부터 시작합니다.

```
waypoint insert <인덱스> <위도> <경도> <고도_ft> <속도_knots> <타입> <지속시간_min> <패턴>
```

| 인자 | 예시 | 단위 | 설명 |
|---|---|---|---|
| 인덱스 | `0` | - | 0부터 14까지 (15개 제한) |
| 위도 | `37.1234567` | 도 (float) | 소수점 7자리 |
| 경도 | `127.1234567` | 도 (float) | 소수점 7자리 |
| 고도 | `400` | feet | 정수 |
| 속도 | `10` | knots | 정수 |
| 타입 | `FLYOVER` | 문자열 | `FLYOVER` / `FLYBY` / `HOLD` / `LAND` / `TAKEOFF` / `ALT_CHANGE` / `DELAY` / `YAW_RATE` |
| 지속시간 | `0` | 분 | `HOLD`/`DELAY` 타입용 |
| 패턴 | `ORBIT` | 문자열 | `ORBIT` / `FIGURE8` |

### waypoint clear
모든 Waypoint를 삭제합니다.

```
waypoint clear
```

### dump / diff
전체 설정을 출력할 때 Waypoint도 함께 표시됩니다.

```
# waypoints
waypoint insert 0 37.1234567 127.1234567 400 10 FLYOVER 0 ORBIT
```

> ⚠️ **저장**: Waypoint는 `save` 명령어로 Flash에 저장되며, 재부팅 후에도 유지됩니다 (`save` 필요).

---

## 6. 비행 중 표시 (OSD)

OSD에 현재 모드와 진행 상태가 나타납니다:

### Ready Mode (OSD_READY_MODE 요소)

| 표시 | 의미 |
|---|---|
| `AUTO-1/4` | Waypoint 미션 진행 중 (현재 1/4) |
| `FLY HOME` | Rescue - Home 귀환 중 |
| `CLIMB` | Rescue - 상승 중 |
| `DESCEND` | Rescue - 하강 중 |
| `SHUT-001` | 셔틀 모드 (왕복 횟수) |

### GPS 좌표 라벨 (GPS 좌표 필드 접두어)

| 표시 | 의미 |
|---|---|
| `W` | Waypoint 방향으로 비행 중 |
| `A` / `B` | 셔틀 A/B 포인트 방향 |
| `H` | Home 귀환 중 |

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

- Home 포인트가 없으면 모든 Waypoint 완료 후 **마지막 Waypoint 주변 선회** 유지
- Home 포인트가 있으면 자동 **Home 귀환**

### 7.3 GPS 손실 시

- 기존 Rescue의 `performSanityChecks()`가 처리
- GPS 손실 감지 시 Rescue 절차에 따라 안전 대응

### 7.4 무장(ARM) 상태 보호

- 비행 중(ARMED)에는 CLI `waypoint insert/clear` 명령이 차단됨
- `waypoint list` 조회만 가능

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

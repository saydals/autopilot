# AUX 스위치를 이용한 비행 중 PID 프로파일 전환 기능 — 검증·수정 계획서 v3

- **대상 저장소**: `github.com/saydals/my-betaflight` (`main` 브랜치)
- **관련 경로**: `src/main/fc/rc_adjustments.h`, `src/main/fc/rc_adjustments.c`, `src/main/config/config.c`
- **검토 기준**: 2026-07-02, `main` 브랜치 실제 소스 재확인 완료
- **문서 버전**: 3.0 — **v2 대비 가장 큰 변화: 코드는 이미 머지되어 있음. 이 문서는 "구현 계획"이 아니라 "검증 + 치명적 버그 수정" 문서.**

---

## 1. 현재 상태 요약

`main` 브랜치를 직접 확인한 결과, v1/v2에서 계획한 6개 코드 변경이 **모두 이미 적용되어 있다.**

| 항목                                                   | 상태                                                |
| ---------------------------------------------------- | ------------------------------------------------- |
| `ADJUSTMENT_PID_PROFILE` enum 추가                     | ✅ 적용됨 (`rc_adjustments.h` line 65, 값=35)          |
| PG 버전 증가                                             | ✅ 적용됨 (`PG_ADJUSTMENT_RANGE_CONFIG, 3`)           |
| `defaultAdjustmentConfigs[]` 항목 추가                   | ✅ 적용됨 (SELECT, `PID_PROFILE_COUNT`)               |
| `adjustmentLabels[]`에 `LED PROFILE`/`PID PROFILE` 추가 | ✅ 적용됨                                             |
| `applySelectAdjustment()` case 추가                    | ✅ 적용됨 (`blackboxLogInflightAdjustmentEvent()` 포함) |
| `updateOsdAdjustmentData()` 중복 표시 예외 추가              | ✅ 적용됨                                             |

**따라서 이 문서에는 코드 diff를 다시 싣지 않는다.** 대신 실제 배포된 코드를 검증하는 과정에서 발견한, **실비행 전에 반드시 고쳐야 하는 문제 1건**과, 그 외 검증/테스트 절차만 다룬다.

---

## 2. 🚨 치명적 문제: CLI `adjrange` function 값이 잘못 계산되어 있음

### 2.1 문제 요약

기존 계획서(v1, v2)는 `adjrange`의 `<function>` 인자에 `ADJUSTMENT_PID_PROFILE`의 **enum 값(35)**을 그대로 쓰면 된다고 가정했다. **이 가정이 틀렸다.**

### 2.2 근거 — 실제 인덱싱 코드 추적

`rc_adjustments.c`에서 `adjrange`로 설정한 함수를 실제로 찾아가는 코드:

```c
#define ADJUSTMENT_FUNCTION_CONFIG_INDEX_OFFSET 1
...
const adjustmentConfig_t *adjustmentConfig =
    &defaultAdjustmentConfigs[adjustmentRange->adjustmentConfig - ADJUSTMENT_FUNCTION_CONFIG_INDEX_OFFSET];
```

즉 CLI에 입력한 숫자는 **enum 값이 아니라 `defaultAdjustmentConfigs[]` 배열 안에서의 위치(1-based)**다. 그런데 이 배열은 `adjustmentFunction_e`와 1:1로 정렬되어 있지 않다 — `ADJUSTMENT_ROLL_RC_RATE`, `ADJUSTMENT_PITCH_RC_RATE`, `ADJUSTMENT_ROLL_RC_EXPO`, `ADJUSTMENT_PITCH_RC_EXPO` 4개 enum 값은 이 배열에 항목 자체가 없다(별도 조합 로직(FALLTHROUGH)으로만 동작하는 값이라 SELECT/adjrange 테이블에서 빠져 있음). 배열 초기화 리스트를 처음부터 끝까지 직접 세어 확인:

```
위치(0-based)  →  실제 함수
...
11             →  ADJUSTMENT_RATE_PROFILE   (enum 12,  아직 안 어긋남)
...
23             →  ADJUSTMENT_HORIZON_STRENGTH (enum 24)
24             →  ADJUSTMENT_PID_AUDIO        (enum 29)  ← 여기서 4칸 건너뜀
25             →  ADJUSTMENT_PITCH_F          (enum 30)
26             →  ADJUSTMENT_ROLL_F           (enum 31)
27             →  ADJUSTMENT_YAW_F            (enum 32)
28             →  ADJUSTMENT_OSD_PROFILE      (enum 33)
29             →  ADJUSTMENT_LED_PROFILE      (enum 34)
30             →  ADJUSTMENT_PID_PROFILE      (enum 35)  ← 우리가 원하는 값
```

`ADJUSTMENT_PID_PROFILE`은 배열 위치 **30**(0-based)에 있다. CLI에 넣을 값은 `위치 + OFFSET(1) = 31`이다.

반면 `adjustmentLabels[]`(OSD 팝업용 문자열 배열)는 이 4개 빈자리도 그대로 채워 넣어 enum과 완전히 1:1로 정렬되어 있다 — **두 배열의 인덱싱 방식이 서로 다르다는 것이 이번 혼동의 근본 원인**이다.

### 2.3 방치 시 결과

CLI 파서(`cliAdjustmentRange()`)는 `val < ADJUSTMENT_FUNCTION_COUNT`(36)만 검사하므로 `35`도 유효한 값으로 통과된다. 하지만 이 값으로 인덱싱하면 `defaultAdjustmentConfigs[34]`를 읽는데, 이 슬롯은 초기화 리스트에 없어 C 표준에 따라 `{ .adjustmentFunction = ADJUSTMENT_NONE, .mode = 0, .data = {0} }`로 자동 채워진 상태다. **크래시도, 에러 메시지도 없이 스위치를 조작해도 아무 반응이 없는 조용한 실패**가 발생한다. 실비행 전 지상 테스트에서 반드시 걸러야 하는 문제다.

### 2.4 수정된 값

| 기능                       | 잘못된 값 (v1/v2) | 올바른 값 (v3) |
| ------------------------ | ------------- | ---------- |
| `ADJUSTMENT_PID_PROFILE` | 35            | **31**     |

### 2.5 이 값의 취약성에 대한 경고

이 숫자는 `defaultAdjustmentConfigs[]`의 **배열 순서**에 의존하는 값이지, enum 값처럼 헤더만 보고 바로 알 수 있는 값이 아니다. 앞으로 이 배열 중간에 새 항목이 추가/삭제되면 이 숫자도 다시 밀린다. **문서에 숫자를 그대로 박아두지 말고, 아래 3장의 절차로 매번 실측 검증할 것을 권장한다.**

---

## 3. 올바른 값 실측 검증 절차 (숫자를 맹신하지 않는 법)

CLI로 직접 확인하는 방법:

```
# 1) 임시로 adjrange 슬롯 하나에 31을 넣고 저장
adjrange 0 0 4 900 2100 31 5 900 2100
save

# 2) dump로 실제 저장된 값 확인
dump
# adjrange 0 0 4 900 2100 31 5 900 2100  ← 그대로 보이면 파싱까지는 정상

# 3) AUX5 스위치를 세 포지션으로 넘기며 CLI 콘솔에 연결한 채로
#    getCurrentPidProfileIndex()가 바뀌는지 확인하는 가장 쉬운 방법은
#    비프음 패턴 확인 (changePidProfile 내부에서 자동으로 울림: 1/2/3회)
```

비프음이 프로파일 번호만큼(1회/2회/3회) 울리면 정상 동작 확인 완료. 만약 스위치를 움직여도 비프가 전혀 안 울리면 function 값이 틀렸거나(가장 먼저 의심할 곳), auxSwitchChannelIndex가 실제 스위치 채널과 다른 것이다.

---

## 4. 사용자 설정 가이드 (수정본)

### 4.1 CLI adjrange 설정

```
# 형식: adjrange <index> <auxCh> <startStep> <endStep> <func> <switchCh> <center> <scale>
# adjustmentFunction 파라미터 = 31 (defaultAdjustmentConfigs[] 내 ADJUSTMENT_PID_PROFILE 위치, enum 값 아님)

adjrange 0 0 4 900 2100 31 5 900 2100
save
diff
```

| 파라미터     | 값        | 의미                                                        |
| -------- | -------- | --------------------------------------------------------- |
| index    | 0~29     | adjrange 슬롯 번호                                            |
| auxCh    | 0        | 모니터링 AUX1 (풀레인지 항상 활성)                                    |
| range    | 900~2100 | 풀레인지                                                      |
| func     | **31**   | `ADJUSTMENT_PID_PROFILE` (defaultAdjustmentConfigs 위치 기준) |
| switchCh | 5        | 3포지션 스위치가 연결된 AUX5                                        |

### 4.2 OSD 표시 활성화 (이미 존재하는 기능, 코드 불필요)

```
set osd_pidrate_profile_pos = 2450   # 원하는 OSD 위치
save
```

### 4.3 추천 스위치 설정

| 스위치           | 전환                            | 권장           |
| ------------- | ----------------------------- | ------------ |
| 3포지션 (AUX5/6) | LOW=PID1, MID=PID2, HIGH=PID3 | ✅ 강력 권장      |
| 2포지션          | LOW=PID1, HIGH=PID2           | ⚠ PID3 전환 불가 |
| 6포지션          | 중간 구간 중복 매핑                   | ❌ 비권장        |

---

## 5. 남은 검증 작업 (테스트 계획)

코드는 이미 있으므로 "구현" 단계는 없고, 아래 검증만 남는다.

### 5.1 지상 테스트

| ID    | 항목                    | 절차                                           | 기대 결과                                    |
| ----- | --------------------- | -------------------------------------------- | ---------------------------------------- |
| GT-01 | function 값 정합성        | 4장 절차대로 `adjrange ... 31 ...` 설정 후 `dump` 확인 | 저장값 그대로 출력                               |
| GT-02 | 3포지션 기본 전환            | LOW→MID→HIGH 순차 이동                           | 비프 1→2→3회, `profile` CLI로 확인 시 인덱스 0→1→2 |
| GT-03 | 동일 위치 재진입             | MID 유지                                       | 추가 비프 없음 (재호출 방지 조건 확인)                  |
| GT-04 | HIGH→LOW 직접 전환        | HIGH→LOW 즉시 이동                               | 프로파일 2→0, 비프 1회                          |
| GT-05 | 서보 점프 여부 (5.1 참고)     | 암 상태에서 프로파일 전환, 서보 출력 관찰                     | 순간 튐 없음 (있다면 5.1 문제로 기록)                 |
| GT-06 | Servo Trim Mode와 상호작용 | 트림 모드 활성 중 PID 프로파일 전환                       | 트림 값(`servoParams()->middle`) 유지 확인      |
| GT-07 | 회귀: 기존 3개 프로파일 전환     | RATE/OSD/LED 프로파일용 기존 adjrange 슬롯 정상 동작 확인   | 이번 변경으로 영향 없음                            |
| GT-08 | EEPROM 영속성            | 프로파일 전환 후 재부팅                                | 마지막 선택 프로파일 유지 (permanent range가 아닌 경우)  |

### 5.2 실비행 테스트

- 저고도, 안정적 기상에서 GT-02~GT-04 반복
- 프로파일 간 게인 차이가 큰 조합(예: 순한 세팅 ↔ 공격적 세팅)에서 전환 시 자세 변화 체감 확인 — 보간 없이 즉시 전환되므로(6.2 참고) 급격한 반응 변화 예상됨을 감안

---

## 6. 남아있는 위험 요소 (검증 필요, 코드 수정 아님)

### 6.1 서보/믹서 재초기화로 인한 순간 출력 변화 (고정익 한정, 위험도: 중간)

`changePidProfile()`은 `initEscEndpoints()`와 `mixerInitProfile()`을 함께 호출한다. 고정익 서보 믹서 경로(서보 트림 모드 등 기존 작업과 맞물리는 영역)에서 전환 순간 서보 출력이 튈 가능성이 있다 — 5.1의 GT-05, GT-06로 검증.

### 6.2 전환 시 보간 없이 즉시 적용됨 (위험도: 낮음, 설계상 특성)

`pidInit()`은 새 게인을 램프업 없이 그 자리에서 즉시 로드한다. RATE_PROFILE 전환도 동일한 특성이라 기존 동작과 일관되지만, 프로파일 간 게인 차이를 지나치게 크게 설계하지 않도록 사용자에게 권장.

### 6.3 EEPROM 저장 정책 (위험도: 낮음, 이미 기존 관례와 일치)

`setConfigDirtyIfNotPermanent()` 흐름은 RATE/OSD/LED 프로파일과 완전히 동일 — 별도 정책 결정 불필요.

### 6.4 Configurator 표시 (위험도: 낮음)

Configurator 클라이언트가 `ADJUSTMENT_PID_PROFILE`을 자체 이름 테이블에 아직 모를 수 있어 Adjustments 탭 드롭다운에서 "UNKNOWN"으로 보일 수 있다. 기능 자체는 CLI로 완전히 검증 가능하므로 펌웨어 쪽 이슈는 아니며, Configurator 쪽 이름 추가는 별도 작업(이 저장소 스코프 밖).

---

## 7. 참고 코드 위치 (빠른 참조, 재확인 완료)

| 항목                                                                | 파일                    | 위치                                         |
| ----------------------------------------------------------------- | --------------------- | ------------------------------------------ |
| `adjustmentFunction_e` enum, `ADJUSTMENT_PID_PROFILE`             | `fc/rc_adjustments.h` | line 65 (값=35)                             |
| `defaultAdjustmentConfigs[]`, PID_PROFILE 실제 위치                   | `fc/rc_adjustments.c` | line 230 부근 (**배열 내 30번째 인덱스 = CLI 값 31**) |
| `adjustmentLabels[]`                                              | `fc/rc_adjustments.c` | line 233~ ("PID PROFILE" 포함)               |
| `PG_REGISTER_ARRAY` (PG 버전)                                       | `fc/rc_adjustments.c` | line 68 (버전 3)                             |
| `ADJUSTMENT_FUNCTION_CONFIG_INDEX_OFFSET`                         | `fc/rc_adjustments.c` | line 670                                   |
| 실제 인덱싱 코드 (`defaultAdjustmentConfigs[adjustmentConfig - OFFSET]`) | `fc/rc_adjustments.c` | line 685, 736, 804                         |
| `applySelectAdjustment()` PID_PROFILE case                        | `fc/rc_adjustments.c` | line 652 부근                                |
| `changePidProfile()`                                              | `config/config.c`     | line 868~883                               |
| `PG_ADJUSTMENT_RANGE_CONFIG` ID                                   | `pg/pg_ids.h`         | line 60 (ID 37)                            |
| `cliAdjustmentRange()` (CLI 파싱, function 값 검증 범위)                 | `cli/cli.c`           | line 1634~                                 |
| `OSD_PIDRATE_PROFILE` 엘리먼트                                        | `osd/osd_elements.c`  | `osdElementPidRateProfile()`               |

---

## 8. v2 → v3 변경 요약

| 항목                                           | v2                          | v3                                                                                                                      |
| -------------------------------------------- | --------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| 문서 성격                                        | 구현 계획서 (Phase M1~M5 포함)     | 검증·버그수정 계획서 (구현 단계 삭제)                                                                                                  |
| 코드 diff 섹션                                   | 6개 지점 diff 전부 수록            | **삭제** — 이미 배포됨                                                                                                         |
| CLI function 값                               | 35 (틀림)                     | **31 (실제 인덱싱 코드로 검증)**                                                                                                  |
| 틀린 값의 원인 설명                                  | 없음                          | **신규**: `defaultAdjustmentConfigs[]`의 4개 빈 슬롯(ROLL/PITCH_RC_RATE, ROLL/PITCH_RC_EXPO) 때문에 배열 위치와 enum 값이 어긋난다는 근본 원인 규명 |
| 값 검증 절차                                      | 없음                          | **신규**: 3장 — 숫자를 다시 안 틀리기 위한 실측 절차                                                                                      |
| 단위 테스트(UT)                                   | enum/배열 크기 static_assert 위주 | **삭제** — 이미 컴파일·머지되어 의미 없음                                                                                              |
| 통합 테스트(IT)                                   | 10개, function=35 기준         | 지상 테스트(GT) 8개로 재구성, function=31 기준 + Servo Trim Mode 상호작용 항목 추가                                                         |
| `blackboxLogInflightAdjustmentEvent()` 누락 여부 | v2 diff 제안에서 누락             | 실제 배포 코드엔 포함되어 있음을 확인 — 문서 표기만 실제와 일치시킴                                                                                 |

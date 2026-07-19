# Rescue / Mission Flight / Infinite Shuttle 시나리오 검증 보고서 (코드 수정 없음)

> 작성일: 2026-07-19  
> 범위: **코드 수정 없이** 문서/기작성 분석을 기반으로 사용자 시나리오가 실제 로직 흐름대로 수행되는지 “검증 관점”에서 정리.  
> 핵심 리스크: **Rescue 해제 시 조종권(사용자 입력)이 확실히 반환되는지 미검증 상태면 안전사고 가능**

---

## 0. 전제(사용자 요구사항 요약)

### 0.1 Rescue 모드(무한셔틀/오토파일럿과의 관계)
- Rescue의 시작에는 “의미 없는” **3초 상승 구간**이 존재
- 이후 **Fly Home에서부터 진짜 Rescue 시작**
- `descent alt`가 **짝수**면: **이륙시 만든 A포인트**를 향해 비행  
  `descent alt`가 **홀수**면: **홈을 향해 비행**
- “홀수 descent alt”에서 홈으로 비행 후 **rescue distance**에 도착하면 **A포인트 생성**
- A포인트 도착 후:
  - (경로1) 바로 Home으로 복귀 또는
  - (경로2) 셔틀 모드로 고도를 낮춘 뒤 Home으로 복귀

### 0.2 무한 셔틀(Infinite Shuttle)
- 비행 중 **GPS 신호가 있으면 아무 때나 발동**
- 기체 기준으로 **2가지 방법으로 A/B 포인트 생성**
- 이후 무한 왕복(셔틀) 비행 수행

### 0.3 미션 비행(Mission Flight)
- 시작 시 Rescue 초기단계를 거침
- `Fly home` 단계 목적지는 **waypoint 1**
- 저장된 모든 waypoint 소진 후, 기존 2가지 Rescue 경로(짝/홀 모드 중 하나)로 전환
- 각 waypoint에는 고도/속도가 있으며 이것이 타겟
- waypoint가 끝나면 고도/속도는 Rescue 시작 초기단계에서 지정한 값을 사용
- waypoint “포인트 동작”이 다양하지만 모두 `fly hover`로 취급(그냥 통과)

### 0.4 모드 전환 규칙(사용자 정의)
- 3가지 비행 단계는 **아무 때나 서로 바꿔 실행 가능**
- 무한 셔틀은 다른 단계 어디에서나 실행되면 현재 기체 기준으로 AB 포인트 생성 후 비행
- Rescue가 설정되면 다른 단계 어디에서나 현재 위치 기준으로 Rescue 모드 실행
- Mission Flight 실행 시 다른 단계 어디에서나 Rescue 초기 작동을 거쳐 `Fly home`에서 mission 수행
- “모드 탭에서는 Rescue 하나로 본다”:
  - Rescue 탭에 할당된 값에 따라 하위 동작이 결정
  - `<= 1400`: Infinite shuttle 가능
  - `1400 ~ 1600`: Mission flight 가능
  - `>= 1600`: 정상 Rescue
- 단, Mission flight에서 **waypoint가 저장되어 있지 않으면 정상 Rescue가 실행**

---

## 1. 이미 확인된 구현/흐름(문서 기반 근거)

### 1.1 제어 권한(사용자 조종권) 반환 타이밍 관련 분석
기존 분석 문서에 따르면 태스크 우선순위/스케줄러 타이밍으로 인해 1-cycle 지연이 존재할 수 있음.

- PID 컨트롤러: REALTIME(최고) → mode bits 업데이트 이전 상태로 동작 가능
- RX task: HIGH priority → mode bits 갱신
- GPS_RESCUE task: MEDIUM priority → rescue 상태/phase 읽기

따라서 **모드 경계 전환 시 한 루프(한 사이클)에서 제어 값이 “이전 모드 값”과 “현재 모드 상태”가 섞여 보일 수 있는 타이밍 갭**이 존재한다는 결론이 문서에 이미 있음.

다만 문서 내 표에서는 다음 항목이 “안전”으로 표시됨:
- Yaw: rescue active일 때만 `gpsRescueGetYawRate()` 호출
- Throttle: rescue active일 때만 `gpsRescueGetThrottle()` 호출
- Roll/Pitch(특정 각도 상태): rescue OFF일 때 pidLevel 호출 안 됨 등

또한 “Minor issue”로:
- `rescueYaw`가 RESCUE_IDLE에서 0으로 리셋되지 않는 불완전 cleanup이 언급됨(즉시 문제는 아닐 수 있으나 정밀 안전 관점에서는 확인 필요).

### 1.2 사용자 시나리오(기능) 구현 여부
기존 분석 문서에는 다음이 “✅ 구현 완료”로 정리돼 있음:
- Rescue 3초 초기 상승 구간
- descentAlt 짝/홀에 따른 A 지점 vs Home 지점 로직
- Fly Home에서 타겟 선택(A vs Home)
- Shuttle 모드 전환
- Infinite shuttle의 AB 포인트 생성 로직
- Mission waypoint 타겟 연결

즉 “구현 자체”는 사용자가 정의한 구조에 부합하는 쪽으로 문서상 판정됨.

### 1.3 “Mission Flight Flickering(순간 조종권이 되었다 잃었다 반복)” 원인 후보
기존 분석 문서에는 flickering의 1차 원인으로:
- Aux 스위치 경계 전환 시 mode bits/mission-to-shuttle 경계가 **한 사이클 타이밍 불일치**를 유발
- 그 결과 PID 입력 값이 mission 값일 때 rescue 상태는 shuttle로 넘어가버려 제어가 흔들리는 현상이 생길 수 있음

또한 2차 원인으로:
- IDLE → missionStart 직후에 `rescueAttainPosition()`을 즉시 호출하여 mission 업데이트 타이밍이 기대와 다를 수 있음
- missionUpdateTargetOnly()가 동일 체인에서 수행되는지, 혹은 다음 루프에 처리되는지를 점검해야 한다는 질문이 문서에 포함됨

---

## 2. 요구사항별 “검증 체크리스트”(코드 수정 없이 확인 방법 중심)

### 2.1 (가장 중요) Rescue 해제 시 사용자 조종권이 **확실히** 반환되는가?
**문서 기반 결론의 한계**:
- 현재 문서에는 “대부분 항목은 안전”이라고 표기되어 있으나,
- “모든 단계에서 조종권 반환이 100% 보장됨”을 **사용자 시나리오 전 단계에 대해 실측 검증까지 완료했는지**는 문서에 명확히 적혀 있지 않음.

따라서 아래 항목은 **필수 실측 확인**으로 남아있음(코드 수정 없이 관측 기반으로 검증):
1. Rescue 단계 중 각 phase에서 사용자 stick 입력이 실제로 어떤 경로로 제어에 반영되는지(특히 ANGLE 모드 포함)
2. Rescue 해제 직후, PID/제어가 즉시 사용자 입력 기준으로 전환되는지
3. AUX 스위치 경계(미션 range/셔틀 range/정상 rescue range 전환)에서 “1-cycle flicker”가 발생하는지

**안전 요구사항(사용자 지정)**:
- “확실하게 검증하지 않으면 큰 사고”이므로, 관측 로그(텔레메트리/blackbox 등)로 모드/phase 전환 순간과 control output의 변화를 비교해야 함.

---

### 2.2 사용자 시나리오대로 비행이 진행되는가?
문서상 구현 대응은 ✅로 되어 있으나, 실제 비행에서 다음은 반드시 시나리오별 관측 확인이 필요:
- descent alt 짝/홀에 따른 A/home 타겟 선택이 실제로 맞는지
- 홀수일 때 홈에서 rescue distance 도착 후 A포인트가 생성되는지
- A포인트 도착 후 “바로 Home” 또는 “셔틀로 고도 낮춘 뒤 Home” 중 실제 선택이 config/phase 규칙대로 수행되는지
- Mission flight:
  - waypoint 저장 유무에 따라 정상 Rescue로 fallback 되는지
  - Fly home 단계 목적지가 waypoint1으로 실제 연결되는지
  - waypoint 모두 소진 후 descentAlt 기반 Rescue 경로로 정확히 전환되는지
  - waypoint 타입별 동작이 모두 fly hover로 “통과” 처리되는지

---

## 3. “자 의적 해석 금지”를 위한 사용자 확인 질문(기존 문서의 질문 그대로 반영)

기존 분석 문서에 이미 아래 3개가 “코드상 의문 → 사용자 뜻 확인 필요”로 정리되어 있음. 안전 관점에서 아래 질문은 **반드시** 사용자 확인이 필요함.

### Q1. Mission entry flow
현재 코드 흐름(문서에 인용됨)에서 IDLE에서 missionStart() 후 즉시 `rescueAttainPosition()`을 호출함.
- 질문: **이 즉시 호출이 의도된 동작**인가?
- 아니면 사용자 의도는 “다음 iteration에서 missionIsActive branch로 처리”하는 방식인가?

### Q2. Waypoint 종료 후 target altitude/height 소스
문서에 기재된 사용자 spec:
- “waypoint가 끝나면 고도나 높이는 레스큐 시작 초기 단계에서 지정한 값을 사용”

코드(문서 추정):
- `returnAltitudeCm` 또는 `initialClimbM` 중 어느 값이 의도인지 불명확

- 질문: 종료 후 고도는 **returnAltitudeCm**가 맞는가, 아니면 **initialClimbM(초기 상승 설정 값)**이 맞는가?

### Q3. Mission 중 infinite shuttle 전환 허용 정책
- 질문: waypoint navigation 중에도 AUX(<1400)로 infinite shuttle을 즉시 발동해도 되는가?
- 아니면 “미션은 완주 후 전환”이어야 하는가?
- 현재 로직은 즉시 전환을 허용하는 쪽으로 분석 문서가 설명하고 있음.

---

## 4. 결론(현재 문서 상태 기준)

1. 사용자 정의의 큰 틀(Rescue/Infinite Shuttle/Mission Flight의 역할 분리 및 전환 가능성)은 문서상 구현과 일치한다고 정리되어 있음.
2. Mission flickering(순간 조종이 되었다 잃었다) 문제는 모드 전환 경계에서의 **one-cycle timing mismatch** 가능성이 주요 원인 후보로 정리되어 있음.
3. 다만 “Rescue 해제 시 사용자 조종권 100% 반환 보장”은 사고 위험이 커서, 문서 기반 분석만으로는 결론 내리기 어려우며 **실측 검증이 필수**로 남아 있음.
4. Q1~Q3는 “자 의적 해석 금지” 조건 때문에 **반드시 사용자 의도 확인**이 필요함.

---

## 5. (다음 단계) 검증 수행 시 필요한 관측 항목(코드 변경 없이)
- Rescue enable/disable 순간의:
  - rescueState.phase
  - mode bits(셔틀/미션/정상 rescue)
  - PID 입력에 사용된 타겟 altitude/velocity 소스
- AUX 스위치 경계 전환 시:
  - mission→shuttle, shuttle→mission 등 경계에서 flicker(한 사이클 흔들림) 발생 여부
- ANGLE_MODE 포함 시:
  - 문서에서 “delayed”로 언급된 stale 사용 구간이 실제로 위험한지 관측

(위 항목은 코드 수정 없이도 로그/OSD/텔레메트리로 관측 가능한 방향으로 진행해야 함.)

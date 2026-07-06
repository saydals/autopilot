# Betaflight Configurator FlightPlan Save 분석 보고서

> 저장소: `https://github.com/betaflight/betaflight-configurator`  
> 분석 대상: Flight Plan "Save" 버튼의 FC 데이터 전송 경로

---

## 1. FlightPlan 관련 파일 구조

| 파일 | 경로 | 역할 |
|---|---|---|
| **FlightPlanTab.vue** | `src/components/tabs/FlightPlanTab.vue` | FlightPlan 탭 UI (Save/Load/Clear 버튼) |
| **useFlightPlan.js** | `src/composables/useFlightPlan.js` | **핵심 비즈니스 로직** |
| **useMspCliSession.js** | `src/composables/useMspCliSession.js` | MSP를 통한 CLI 명령 전송 |
| **msp.js** | `src/js/msp.js` | MSP 프로토콜 + CLI 세션 관리 |
| WaypointList.vue | `src/components/tabs/FlightPlan/WaypointList.vue` | Waypoint 목록 표시 |
| FlightPlanMap.vue | `src/components/tabs/FlightPlan/FlightPlanMap.vue` | 지도 표시 |
| WaypointEditor.vue | `src/components/tabs/FlightPlan/WaypointEditor.vue` | Waypoint 편집 다이얼로그 |

---

## 2. Save 버튼 — UI에서 이벤트 연결

**파일**: `src/components/tabs/FlightPlanTab.vue`

```vue
<!-- Save 버튼 -->
<UButton :disabled="!canUseFC" :title="$t('flightPlanSaveToFC')" @click="handleSave">
    {{ $t("save") }}
</UButton>
```

```javascript
const { saveToFC } = useFlightPlan();

const handleSave = async () => {
    if (!canUseFC.value) {
        return;
    }
    await saveToFC();
};
```

> 📌 `handleSave()` → `saveToFC()` 호출

---

## 3. `saveToFC()` — 실제 FC 전송 로직

**파일**: `src/composables/useFlightPlan.js`

```javascript
const saveToFC = async () => {
    try {
        const sorted = [...state.waypoints].sort((a, b) => a.order - b.order);
        await sendCliCommand("waypoint clear");
        for (let i = 0; i < sorted.length; i++) {
            await sendCliCommand(waypointToCliCommand(sorted[i], i));
        }
        await sendCliCommand("save");
        gui_log(i18n.getMessage("flightPlanSavedToFC"));
        savePlan();
    } catch (error) {
        console.error("Failed to save flight plan to FC:", error);
    }
};
```

> 📌 **3단계**: `waypoint clear` → `waypoint insert ...` (반복) → `save`

---

## 4. CLI 명령어 생성 형식

**파일**: `src/composables/useFlightPlan.js`

```javascript
const FEET_TO_CM = 30.48;
const KNOTS_TO_CMS = 51.4444;
const MINUTES_TO_DECISECONDS = 600;
```

실제 전송 예시:
```
waypoint insert 0 37.1234567 127.1234567 400 10 FLYOVER 0 ORBIT
waypoint insert 1 37.2345678 127.2345678 300 15 FLYBY 2 FIGURE8
```

---

## 5. 타입/패턴 매핑

### Waypoint 타입

```javascript
const TYPE_TO_CLI = {
    flyover: "FLYOVER", flyby: "FLYBY", hold: "HOLD",
    land: "LAND", takeoff: "TAKEOFF",
    alt_change: "ALT_CHANGE", delay: "DELAY", yaw_rate: "YAW_RATE",
};
```

### Waypoint 패턴

```javascript
const PATTERN_TO_CLI = {
    circle: "ORBIT", orbit: "ORBIT", figure8: "FIGURE8",


---

## 6. `sendCliCommand()` → MSP CLI 전송 체인

**파일**: `src/composables/useMspCliSession.js`

```javascript
import MSP from "../js/msp";

export function send(command, { timeoutMs = 2000 } = {}) {
    return new Promise((resolve, reject) => {
        MSP.send_cli_command(
            command,
            (lines, error) => {
                if (error) { reject(error); return; }
                resolve(Array.isArray(lines) ? [...lines] : []);
            },
            { timeoutMs },
        );
    });
}
```

> 📌 `send()` → `MSP.send_cli_command()` 호출

---

## 7. MSP CLI 프로토콜 — RAW 텍스트 전송

**파일**: `src/js/msp.js`

```javascript
// MSP decoder에 CLI_COMMAND 상태 (18) 존재
decoder_states: { ... CLI_COMMAND: 18, ... }

// CLI 관련 속성
cli_buffer: [], cli_output: [], cli_callback: null,
cli_queue: [], cli_in_flight: null, cli_timer: null,
```

CLI 수신 처리:
```javascript
case this.decoder_states.CLI_COMMAND:
    switch (chunk) {
        case 0x03:  // END_OF_TEXT
            this.cli_callback(this.cli_output);  // 응답 반환
            this.state = this.decoder_states.IDLE;
            break;
        case 0x0a:  // LINE_FEED
            this.cli_output.push(this.cli_buffer.join(""));
            this.cli_buffer.length = 0;
            break;
        default:
            this.cli_buffer.push(String.fromCharCode(chunk));
            break;
    }
```

> 📌 **MSP 명령 번호 사용 안 함** — CLI_COMMAND 상태(18)에서 RAW 텍스트 전송

---

## 8. `loadFromFC()` — FC에서 불러오기

**파일**: `src/composables/useFlightPlan.js`

```javascript
const loadFromFC = async () => {
    try {
        const response = await sendCliCommand("waypoint list");
        const lines = response.filter(l => l.startsWith("waypoint insert"));
        const waypoints = [];
        for (const line of lines) {
            const parts = line.split(" ");
            const wp = {
                order: parseInt(parts[2]),
                latitude: parseFloat(parts[3]),
                longitude: parseFloat(parts[4]),
                altitude: parseFloat(parts[5]) * 30.48,
                speed: parseFloat(parts[6]) * 51.4444,
                type: CLI_TO_TYPE[parts[7]] || "flyover",
            };
            waypoints.push(wp);
        }
        state.waypoints = waypoints;
    } catch (error) {
        loadPlan(); // localStorage Fallback
    }
};
```

---

## 9. 전체 호출 흐름도

```
FlightPlanTab.vue
│
├─ [Save 버튼] @click="handleSave"
│   └─ saveToFC()
│       ├─ sendCliCommand("waypoint clear")        ← CLI
│       ├─ sendCliCommand("waypoint insert ...")    ← CLI (반복)
│       └─ sendCliCommand("save")                   ← CLI + 재부팅
│           └─ useMspCliSession.send()
│               └─ MSP.send_cli_command()
│                   └─ serial.send("cmd\\r\\n")     ← RAW 시리얼
│
└─ [Load 버튼] @click="handleLoad"
    └─ loadFromFC() → sendCliCommand("waypoint list") → 응답 파싱
```

---

## 10. 결론

**Configurator는 FlightPlan Save 버튼을 통해 waypoint를 FC로 정상 전송합니다.**

### 전송 명령어
```
waypoint clear
waypoint insert <idx> <lat> <lon> <alt_ft> <speed_knots> <type> <duration_min> <pattern>
waypoint insert ...  (반복)
save
```

### Configurator 측 구현 상태

| 항목 | 상태 | 근거 |
|---|---|---|
| Save 버튼 | ✅ | `FlightPlanTab.vue` → `@click="handleSave"` |
| CLI 생성 | ✅ | `useFlightPlan.js` → `waypointToCliCommand()` |
| 시리얼 전송 | ✅ | `useMspCliSession.send()` → `MSP.send_cli_command()` |
| 응답 파싱 | ✅ | `loadFromFC()` → `"waypoint insert"` 파싱 |
| MSP 명령 사용 | ❌ 미사용 | CLI_COMMAND state (18), RAW 텍스트 |

### 저장 실패 원인 (추정)

Configurator는 정상 전송하므로 **펌웨어 측**을 확인해야 합니다:

1. **`save` → 재부팅** → `gpsRescueInit()` → `missionInit()`에서 PG 복원 경로
2. **`waypoint insert` 파서**가 8개 인자 형식 처리 확인
3. **PG 동기화** (`missionInsert()` → `missionConfigMutable()`)가 CLI 경로에서 정상 호출되는지 확인
```
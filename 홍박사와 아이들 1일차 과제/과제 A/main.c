#define F_CPU 1000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>

// ===============================================================
//                     공통 설정 / 상수
// ===============================================================

// TM1650 시간 표현: 최대 9:59.9 → 5999 데시초
#define TIME_DS_MAX            5999U

// 타이머 주기
#define TICK_1MS_OCR0A         124     // 1MHz / 8 / (124+1) = 1000Hz → 1ms
#define STOPWATCH_TICK_MS      100U    // 0.1초 = 100ms
#define BOMB_TICK_MS           100U    // 0.1초 = 100ms

// 도트/깜빡임 주기
#define DOT_BLINK_MS           500U    // 0.5초
#define BOMB_IDLE_BLINK_MS     300U

// 폭탄 부저 규칙
#define BOMB_BEEP_INTERVAL_LONG_MS    2000U  // 10초 초과
#define BOMB_BEEP_INTERVAL_MID_MS     1000U  // 5~10초
#define BOMB_BEEP_INTERVAL_SHORT_MS    500U  // 1~5초
#define BOMB_BEEP_INTERVAL_3S_MS       250U  // 1~3초
#define BOMB_BEEP_INTERVAL_1S_MS       125U  // 1초 이하
#define BOMB_BEEP_PULSE_MS             100U
#define BOMB_BEEP_PULSE_1S_MS           50U

// 부저 기본 주파수 (시한폭탄 카운트용)
#define BUZZER_BASE_FREQ_HZ    4000U

// ===============================================================
//                     I2C (소프트웨어) / TM1650
// ===============================================================

#define SDA_PORT PORTB
#define SDA_DDR  DDRB
#define SDA_PINR PINB
#define SDA_BIT  PB0

#define SCL_PORT PORTB
#define SCL_DDR  DDRB
#define SCL_BIT  PB2

static void i2c_delay(void) {
	_delay_us(1);
}

// SDA = 입력(Hi-Z) → 풀업에 의해 High로 끌어올려짐.
static void i2c_sda_high(void) {
	SDA_DDR &= ~(1 << SDA_BIT);
}

// SDA = 출력 Low
static void i2c_sda_low(void) {
	SDA_DDR |= (1 << SDA_BIT);
	SDA_PORT &= ~(1 << SDA_BIT);
}

// SCL = 입력(Hi-Z). 동일하게 풀업.
static void i2c_scl_high(void) {
	SCL_DDR &= ~(1 << SCL_BIT);
}

// SCL = 출력 Low
static void i2c_scl_low(void) {
	SCL_DDR |= (1 << SCL_BIT);
	SCL_PORT &= ~(1 << SCL_BIT);
}

// START: SCL High 동안 SDA High → Low
static void i2c_start(void) {
	i2c_sda_high();
	i2c_scl_high();
	i2c_delay();
	i2c_sda_low();
	i2c_delay();
	i2c_scl_low();
}

// STOP: SCL High 동안 SDA Low → High
static void i2c_stop(void) {
	i2c_sda_low();
	i2c_delay();
	i2c_scl_high();
	i2c_delay();
	i2c_sda_high();
	i2c_delay();
}

// 1비트 송신
static void i2c_write_bit(uint8_t bit) {
	if (bit) i2c_sda_high();
	else     i2c_sda_low();

	i2c_delay();
	i2c_scl_high();
	i2c_delay();
	i2c_scl_low();
}

// 슬레이브 ACK 비트 읽기 (0=ACK, 1=NACK)
static uint8_t i2c_read_ack(void) {
	uint8_t ack;

	i2c_sda_high();       
	i2c_delay();
	i2c_scl_high();
	i2c_delay();
	ack = (SDA_PINR & (1 << SDA_BIT)) ? 1 : 0;
	i2c_scl_low();
	return ack;
}

// 1바이트 송신 후 ACK 읽기 (ACK 값은 무시)
static void i2c_write_byte(uint8_t data) {
	for (int8_t i = 7; i >= 0; i--) {
		i2c_write_bit((data >> i) & 0x01);
	}
	(void)i2c_read_ack();
}

// ack=1 → ACK(계속 읽기), ack=0 → NACK(마지막 바이트)
static uint8_t i2c_read_byte(uint8_t ack) {
	uint8_t data = 0;

	i2c_sda_high();    // 입력 모드

	for (int8_t i = 7; i >= 0; i--) {
		i2c_delay();
		i2c_scl_high();
		i2c_delay();
		if (SDA_PINR & (1 << SDA_BIT)) {
			data |= (1 << i);
		}
		i2c_scl_low();
	}

	// ACK/NACK 전송
	if (ack) {
		// ACK → SDA Low
		i2c_sda_low();
		} else {
		// NACK → SDA 릴리즈
		i2c_sda_high();
	}
	i2c_delay();
	i2c_scl_high();
	i2c_delay();
	i2c_scl_low();
	i2c_sda_high();

	return data;
}

// ----------------- TM1650 상위 함수 -----------------

// TM1650 주소 (7비트)
#define TM1650_CTRL_ADDR  0x24  // 컨트롤 + 키 입력
#define TM1650_DIG1_ADDR  0x34
#define TM1650_DIG2_ADDR  0x35
#define TM1650_DIG3_ADDR  0x36
#define TM1650_DIG4_ADDR  0x37

// 7세그 숫자 테이블 (DP 제외)
static const uint8_t seg_table[10] = {
	0x3F, // 0
	0x06, // 1
	0x5B, // 2
	0x4F, // 3
	0x66, // 4
	0x6D, // 5
	0x7D, // 6
	0x07, // 7
	0x7F, // 8
	0x6F  // 9
};

// TM1650에 1바이트 쓰기
static void tm1650_write(uint8_t addr, uint8_t data) {
	i2c_start();
	i2c_write_byte((addr << 1) | 0);  // write 모드
	i2c_write_byte(data);
	i2c_stop();
}

// TM1650 컨트롤 주소에서 키코드 1바이트 읽기
static uint8_t tm1650_read_key_raw(void) {
	uint8_t v;

	i2c_start();
	i2c_write_byte((TM1650_CTRL_ADDR << 1) | 1); // read 모드
	v = i2c_read_byte(0);                        // 마지막 바이트 → NACK
	i2c_stop();

	return v;
}

// 키코드를 1~4번 버튼으로 매핑 (없으면 0)
static int8_t tm1650_get_button_1_to_4(void) {
	uint8_t key = tm1650_read_key_raw();

	if      (key == 0xE4) return 1;
	else if (key == 0xDC) return 2;
	else if (key == 0xD4) return 3;
	else if (key == 0xCC) return 4;
	else                  return 0;
}

// TM1650 초기화: 디스플레이 ON, 밝기 설정, "0000" 출력
static void tm1650_init(void) {
	i2c_sda_high();
	i2c_scl_high();
	_delay_ms(50);

	// 디스플레이 ON + 밝기 설정 (데이터시트 기준 0x87 사용)
	tm1650_write(TM1650_CTRL_ADDR, 0x87);

	tm1650_write(TM1650_DIG1_ADDR, seg_table[0]);
	tm1650_write(TM1650_DIG2_ADDR, seg_table[0]);
	tm1650_write(TM1650_DIG3_ADDR, seg_table[0]);
	tm1650_write(TM1650_DIG4_ADDR, seg_table[0]);
}

// ds(0.1초)를 4자리 7세그 코드로 변환
static void tm1650_decode_time_ds(uint16_t ds,
uint8_t *d1,
uint8_t *d2,
uint8_t *d3,
uint8_t *d4) {
	if (ds > TIME_DS_MAX) ds = TIME_DS_MAX;

	uint8_t minutes  = ds / 600;          // 60s = 600ds
	uint8_t tens_sec = (ds / 100) % 6;    // 10초 자리 (0~5)
	uint8_t sec      = (ds / 10)  % 10;   // 1초 자리
	uint8_t tenth    = ds % 10;           // 0.1초

	*d1 = seg_table[minutes];
	*d2 = seg_table[tens_sec];
	*d3 = seg_table[sec];
	*d4 = seg_table[tenth];
}

// ds(0.1초) 표시, dot_on==1이면 초 자리 DP 켬
static void tm1650_display_time_ds(uint16_t ds, uint8_t dot_on) {
	uint8_t d1, d2, d3, d4;
	tm1650_decode_time_ds(ds, &d1, &d2, &d3, &d4);

	if (dot_on) {
		d3 |= 0x80;   // 초 자리 소수점(DP) 비트
	}

	tm1650_write(TM1650_DIG1_ADDR, d1);
	tm1650_write(TM1650_DIG2_ADDR, d2);
	tm1650_write(TM1650_DIG3_ADDR, d3);
	tm1650_write(TM1650_DIG4_ADDR, d4);
}

// ds(0.1초) + 특정 자리 깜빡임 표시
// selected_digit: 0~3 (분, 10초, 1초, 0.1초), blink_on==0일 때 해당 자리 끔
static void tm1650_display_time_ds_with_blink(uint16_t ds,
uint8_t selected_digit,
uint8_t blink_on) {
	uint8_t d1, d2, d3, d4;
	tm1650_decode_time_ds(ds, &d1, &d2, &d3, &d4);

	if (!blink_on) {
		switch (selected_digit) {
			case 0: d1 = 0x00; break;
			case 1: d2 = 0x00; break;
			case 2: d3 = 0x00; break;
			case 3: d4 = 0x00; break;
			default: break;
		}
	}

	tm1650_write(TM1650_DIG1_ADDR, d1);
	tm1650_write(TM1650_DIG2_ADDR, d2);
	tm1650_write(TM1650_DIG3_ADDR, d3);
	tm1650_write(TM1650_DIG4_ADDR, d4);
}

// ===============================================================
//                     Timer0 (1ms 시스템 타이머)
// ===============================================================

volatile uint32_t g_millis = 0;

ISR(TIMER0_COMPA_vect) {
	g_millis++;
}

static void timer0_init(void) {
	TCCR0A = 0;
	TCCR0B = 0;
	TCNT0  = 0;

	// CTC 모드
	TCCR0A |= (1 << WGM01);

	OCR0A = TICK_1MS_OCR0A;

	// 비교 일치 인터럽트 enable
	TIMSK |= (1 << OCIE0A);

	// 분주 8
	TCCR0B |= (1 << CS01);
}

// 인터럽트 안전하게 1ms 단위 시간 읽기
static uint32_t millis_read(void) {
	uint32_t m;

	cli();
	m = g_millis;
	sei();

	return m;
}

// ===============================================================
//                         부저 (Timer1)
// ===============================================================

#define BUZZER_PIN  PB4
#define BUZZER_DDR  DDRB
#define BUZZER_PORT PORTB

void buzzer_stop(void);

// 원하는 주파수로 PB4에서 PWM 출력
void buzzer_set_frequency(uint16_t frequency_hz) {
	if (frequency_hz == 0) {
		buzzer_stop();
		return;
	}

	// Timer1 정지/초기화
	TCCR1 = 0;
	GTCCR = 0;
	TCNT1 = 0;

	// 1MHz / 1 / (OCR1C+1) = f
	uint32_t top32 = (F_CPU / frequency_hz) - 1;
	if (top32 > 255) top32 = 255;
	uint8_t top = (uint8_t)top32;

	OCR1C = top;
	OCR1B = top / 2;  // 50% duty

	// PWM1B enable, OC1B non-inverting
	GTCCR = (1 << PWM1B) | (1 << COM1B1);

	// PB4 출력
	BUZZER_DDR |= (1 << BUZZER_PIN);

	// 분주 1
	TCCR1 = (1 << CS10);
}

void buzzer_stop(void) {
	TCCR1 = 0;
	GTCCR = 0;
	BUZZER_PORT &= ~(1 << BUZZER_PIN);
}

// 비차단형 부저 제어용 상태
static uint8_t  buzzer_active       = 0;
static uint8_t  buzzer_continuous   = 0;
static uint32_t buzzer_off_deadline = 0;

// length_ms 동안만 짧게 울리는 펄스
static void buzzer_pulse_start(uint32_t now_ms, uint16_t length_ms) {
	buzzer_set_frequency(BUZZER_BASE_FREQ_HZ);
	buzzer_active       = 1;
	buzzer_continuous   = 0;
	buzzer_off_deadline = now_ms + length_ms;
}

// 연속 울림 ON/OFF
static void buzzer_set_continuous(uint8_t on) {
	if (on) {
		buzzer_set_frequency(BUZZER_BASE_FREQ_HZ);
		buzzer_continuous = 1;
		buzzer_active     = 1;
		} else {
		buzzer_continuous = 0;
		buzzer_active     = 0;
		buzzer_stop();
	}
}

// 펄스 종료 타이밍만 관리 (main 루프에서 호출)
static void buzzer_update(uint32_t now_ms) {
	if (buzzer_active && !buzzer_continuous) {
		if ((int32_t)(now_ms - buzzer_off_deadline) >= 0) {
			buzzer_active = 0;
			buzzer_stop();
		}
	}
}

// ===============================================================
//                        폭발 멜로디
// ===============================================================

typedef enum {
	MELODY_NONE = 0,
	MELODY_PLAYING
} MelodyState;

static MelodyState melody_state        = MELODY_NONE;
static uint8_t     melody_step         = 0;
static uint32_t    melody_step_deadline = 0;

// 간단한 "띠-띠-띠-" 느낌 멜로디 (Hz, ms)
static const uint16_t melody_freqs[] = { 3000,    0,  3800,    0,  4500 };
static const uint16_t melody_durs[]  = {   80,   40,    80,   40,   200 };
#define MELODY_LEN (sizeof(melody_freqs) / sizeof(melody_freqs[0]))

static void melody_start(uint32_t now_ms) {
	melody_state         = MELODY_PLAYING;
	melody_step          = 0;
	melody_step_deadline = now_ms;      // 바로 첫 음 시작
}

// 멜로디 상태 업데이트 (비차단)
// main 루프에서 주기적으로 호출
static void melody_update(uint32_t now_ms) {
	if (melody_state != MELODY_PLAYING) return;

	// 현재 노트 지속시간이 아직 안 지났으면 대기
	if ((int32_t)(now_ms - melody_step_deadline) < 0) return;

	// 멜로디 끝
	if (melody_step >= MELODY_LEN) {
		melody_state = MELODY_NONE;
		buzzer_stop();
		return;
	}

	uint16_t f = melody_freqs[melody_step];
	uint16_t d = melody_durs[melody_step];

	if (f == 0) {
		// 쉼표
		buzzer_stop();
		} else {
		buzzer_set_frequency(f);
	}

	melody_step_deadline = now_ms + d;
	melody_step++;
}

// ===============================================================
//                            LED
// ===============================================================

#define LED1_PIN PB3    // 시한폭탄 완료/경고 표시
#define LED2_PIN PB1    // 스톱워치 동작 표시

static void led_init(void) {
	DDRB  |= (1 << LED1_PIN) | (1 << LED2_PIN);
	PORTB &= ~(1 << LED1_PIN);
	PORTB |= (1 << LED2_PIN);
}

static inline void led1_on(void)      { PORTB |=  (1 << LED1_PIN); }
static inline void led1_off(void)     { PORTB &= ~(1 << LED1_PIN); }
static inline void led2_on(void)      { PORTB |=  (1 << LED2_PIN); }
static inline void led2_off(void)     { PORTB &= ~(1 << LED2_PIN); }

// ===============================================================
//                     버튼 이벤트 플래그
// ===============================================================

volatile uint8_t btn1_clicked = 0;
volatile uint8_t btn2_clicked = 0;
volatile uint8_t btn3_clicked = 0;
volatile uint8_t btn4_clicked = 0;

// ===============================================================
//                         모드 / 상태
// ===============================================================

typedef enum {
	MODE_STOPWATCH = 0,
	MODE_BOMBTIMER = 1
} Mode;

typedef enum {
	BOMB_IDLE = 0,   // 프리셋 설정 상태
	BOMB_RUNNING,
	BOMB_PAUSED,
	BOMB_DONE
} BombState;

typedef enum {
	SW_STATE_RESET = 0,
	SW_STATE_RUNNING,
	SW_STATE_PAUSED
} StopwatchState;

static Mode           g_mode       = MODE_STOPWATCH;
static BombState      g_bomb_state = BOMB_IDLE;
static StopwatchState g_sw_state   = SW_STATE_RESET;

// 데시초 (0.1초 단위)
static uint16_t stopwatch_ds       = 0;
static uint16_t bomb_ds            = 300;   // 기본값: 30.0초
static uint16_t bomb_preset_ds     = 300;

static uint8_t  bomb_selected_digit = 0;    // 0:분, 1:10초, 2:1초, 3:0.1초

// 타이밍 관리용
static uint32_t last_sw_tick_ms    = 0;
static uint32_t last_bomb_tick_ms  = 0;
static uint32_t last_dot_toggle_ms = 0;
static uint8_t  dot_on             = 1;

static uint32_t last_bomb_beep_ms  = 0;

// ===============================================================
//                     공통: 모드 버튼 처리
//                         (버튼1: 모드 전환)
// ===============================================================

static void handle_mode_button(void) {
	if (!btn1_clicked) return;

	btn1_clicked = 0;
	uint32_t now = millis_read();

	if (g_mode == MODE_STOPWATCH) {
		// STOPWATCH → BOMBTIMER 전환
		// 폭탄이 이미 RUNNING 상태였다면, STOPWATCH 모드에 있는 동안 지난 시간을
		// 한 번에 반영하여 시간을 점프시킴 (숨은 카운트 유지)
		if (g_bomb_state == BOMB_RUNNING) {
			uint32_t elapsed_ms = now - last_bomb_tick_ms;
			uint16_t elapsed_ds = elapsed_ms / BOMB_TICK_MS;

			if (elapsed_ds > 0) {
				if (elapsed_ds >= bomb_ds) {
					bomb_ds = 0;
					} else {
					bomb_ds -= elapsed_ds;
				}
				// 기준 시각 재조정 (100ms 격자 맞추기)
				last_bomb_tick_ms = now - (elapsed_ms % BOMB_TICK_MS);
			}
		}

		g_mode = MODE_BOMBTIMER;

		led1_on();          // 폭탄 모드 표시
		led2_off();
		buzzer_set_continuous(0);
		} else {
		// BOMBTIMER → STOPWATCH 전환
		g_mode = MODE_STOPWATCH;

		led2_on();          // 스톱워치 모드 표시
		led1_off();
		buzzer_set_continuous(0);
	}

	dot_on = 1;
	last_dot_toggle_ms = now;
}

// ===============================================================
//                        스톱워치 처리
//
//  - 모드: MODE_STOPWATCH
//  - 버튼2: start/pause
//  - 버튼3: reset
// ===============================================================

static void handle_stopwatch_buttons(uint32_t now_ms) {
	if (btn2_clicked) {
		btn2_clicked = 0;

		if (g_sw_state == SW_STATE_RUNNING) {
			g_sw_state = SW_STATE_PAUSED;
			} else {
			// reset 또는 paused → running
			g_sw_state      = SW_STATE_RUNNING;
			last_sw_tick_ms = now_ms;
		}
	}

	if (btn3_clicked) {
		btn3_clicked = 0;
		g_sw_state   = SW_STATE_RESET;
		stopwatch_ds = 0;
	}
}

static void update_stopwatch(uint32_t now_ms) {
	if (g_mode != MODE_STOPWATCH) return;

	handle_stopwatch_buttons(now_ms);

	if (g_sw_state == SW_STATE_RESET) {
		stopwatch_ds = 0;
		} else if (g_sw_state == SW_STATE_RUNNING) {

		// 0.1초마다 데시초 1 증가
		while ((int32_t)(now_ms - last_sw_tick_ms) >= (int32_t)STOPWATCH_TICK_MS) {
			last_sw_tick_ms += STOPWATCH_TICK_MS;
			if (stopwatch_ds < TIME_DS_MAX) {
				stopwatch_ds++;
			}
		}
		} else { // PAUSED
	}

	// 스톱워치 도트: 0.5초마다 깜빡
	if ((int32_t)(now_ms - last_dot_toggle_ms) >= (int32_t)DOT_BLINK_MS) {
		last_dot_toggle_ms += DOT_BLINK_MS;
		dot_on = !dot_on;
	}

	tm1650_display_time_ds(stopwatch_ds, dot_on);
}

// ===============================================================
//                        시한폭탄 처리
//
//  - 모드: MODE_BOMBTIMER
//  - 상태: BOMB_IDLE / RUNNING / PAUSED / DONE
//  - 버튼2: start / pause / resume / (DONE 에서) reset
//  - 버튼3: 선택 자리 값 증가 (IDLE/PAUSED 에서만)
//  - 버튼4: 자리 선택 (0~3 순환)
// ===============================================================

// 프리셋 시간에서 선택된 자리 증가
static void bomb_increase_selected_digit(void) {
	uint16_t ds = bomb_preset_ds;

	uint8_t minutes  = ds / 600;
	uint8_t tens_sec = (ds / 100) % 6;
	uint8_t sec      = (ds / 10)  % 10;
	uint8_t tenth    = ds % 10;

	switch (bomb_selected_digit) {
		case 0: // 분 (0~9)
		minutes = (minutes + 1) % 10;
		break;
		case 1: // 10초 (0~5)
		tens_sec = (tens_sec + 1) % 6;
		break;
		case 2: // 1초 (0~9)
		sec = (sec + 1) % 10;
		break;
		case 3: // 0.1초 (0~9)
		tenth = (tenth + 1) % 10;
		break;
		default:
		break;
	}

	bomb_preset_ds = minutes * 600 + tens_sec * 100 + sec * 10 + tenth;
	if (bomb_preset_ds > TIME_DS_MAX) bomb_preset_ds = TIME_DS_MAX;
}

// 버튼2/3/4 처리
static void handle_bomb_buttons(void) {
	if (btn2_clicked) {
		btn2_clicked = 0;

		if (g_bomb_state == BOMB_IDLE) {
			// 처음 시작
			bomb_ds            = bomb_preset_ds;
			g_bomb_state       = BOMB_RUNNING;
			last_bomb_tick_ms  = millis_read();
			last_bomb_beep_ms  = last_bomb_tick_ms;
			} else if (g_bomb_state == BOMB_RUNNING) {
			// 일시정지
			g_bomb_state = BOMB_PAUSED;
			buzzer_set_continuous(0);
			} else if (g_bomb_state == BOMB_PAUSED) {
			// 다시 시작
			g_bomb_state      = BOMB_RUNNING;
			last_bomb_tick_ms = millis_read();
			last_bomb_beep_ms = last_bomb_tick_ms;
			} else if (g_bomb_state == BOMB_DONE) {
			// DONE → 다시 IDLE 상태로 초기화
			g_bomb_state  = BOMB_IDLE;
			bomb_ds       = bomb_preset_ds;
			buzzer_set_continuous(0);
			led1_off();
		}
	}

	if (btn3_clicked) {
		btn3_clicked = 0;
		if (g_bomb_state == BOMB_IDLE || g_bomb_state == BOMB_PAUSED) {
			bomb_increase_selected_digit();
			bomb_ds = bomb_preset_ds;
		}
	}

	if (btn4_clicked) {
		btn4_clicked = 0;
		if (g_bomb_state == BOMB_IDLE || g_bomb_state == BOMB_PAUSED) {
			bomb_selected_digit = (uint8_t)((bomb_selected_digit + 1) & 0x03); // 0~3 순환
		}
	}
}

// 폭탄용 부저 규칙:
//   - 10초 초과: 2초 간격
//   - 5~10초: 1초 간격
//   - 3~5초: 0.5초 간격
//   - 1~3초: 0.25초 간격
//   - 1초 이하: 0.125초 간격 짧게 삑
//   - 0초: 멜로디 + LED1 ON
static void update_bomb_buzzer(uint32_t now_ms) {
	// 멜로디 재생 중이면 멜로디만 갱신
	if (melody_state == MELODY_PLAYING) {
		melody_update(now_ms);
		return;
	}

	// 폭탄이 RUNNING 상태가 아니면, 규칙에 따른 비프 없음
	if (g_bomb_state != BOMB_RUNNING) {
		buzzer_set_continuous(0);
		return;
	}

	uint16_t ds       = bomb_ds;
	uint32_t remain_ms = (uint32_t)ds * 100U;

	// 0으로 떨어진 시점 → 폭발 처리 + 멜로디 시작
	if (ds == 0) {
		g_bomb_state = BOMB_DONE;
		buzzer_set_continuous(0);
		melody_start(now_ms);
		return;
	}

	// 1초 이하: 촘촘하게 짧게 삑
	if (remain_ms <= 1000U) {
		uint16_t interval_ms = BOMB_BEEP_INTERVAL_1S_MS;
		if ((int32_t)(now_ms - last_bomb_beep_ms) >= (int32_t)interval_ms) {
			last_bomb_beep_ms = now_ms;
			buzzer_pulse_start(now_ms, BOMB_BEEP_PULSE_1S_MS);
		}
		return;
	}

	// 3초 이하: 0.25초 간격
	if (remain_ms <= 3000U) {
		uint16_t interval_ms = BOMB_BEEP_INTERVAL_3S_MS;
		if ((int32_t)(now_ms - last_bomb_beep_ms) >= (int32_t)interval_ms) {
			last_bomb_beep_ms = now_ms;
			buzzer_pulse_start(now_ms, BOMB_BEEP_PULSE_MS);
		}
		return;
	}

	// 그 외 구간: 5/10초 기준 기존 규칙
	uint16_t interval_ms;
	if (remain_ms > 10000U) {            // 10초 초과
		interval_ms = BOMB_BEEP_INTERVAL_LONG_MS;
		} else if (remain_ms > 5000U) {      // 5~10초
		interval_ms = BOMB_BEEP_INTERVAL_MID_MS;
		} else {                             // 3~5초
		interval_ms = BOMB_BEEP_INTERVAL_SHORT_MS;
	}

	if ((int32_t)(now_ms - last_bomb_beep_ms) >= (int32_t)interval_ms) {
		last_bomb_beep_ms = now_ms;
		buzzer_pulse_start(now_ms, BOMB_BEEP_PULSE_MS);
	}
}

// 폭탄 타이머 전체 상태 업데이트 + 표시
static void update_bomb(uint32_t now_ms) {
	if (g_mode != MODE_BOMBTIMER) return;

	handle_bomb_buttons();

	// 카운트다운 (RUNNING 상태에서만)
	if (g_bomb_state == BOMB_RUNNING) {
		if ((int32_t)(now_ms - last_bomb_tick_ms) >= (int32_t)BOMB_TICK_MS) {
			last_bomb_tick_ms += BOMB_TICK_MS;
			if (bomb_ds > 0) {
				bomb_ds--;
			}
		}
	}

	// ==== 디스플레이 처리 ====

	if (g_bomb_state == BOMB_RUNNING) {
		// 러닝 중: 도트 0.5초 깜빡 + 남은 시간 표시
		if ((int32_t)(now_ms - last_dot_toggle_ms) >= (int32_t)DOT_BLINK_MS) {
			last_dot_toggle_ms += DOT_BLINK_MS;
			dot_on = !dot_on;
		}
		tm1650_display_time_ds(bomb_ds, dot_on);

		} else if (g_bomb_state == BOMB_DONE) {
		// DONE: 0.0 고정 (도트 ON)
		tm1650_display_time_ds(bomb_ds, 1);

		} else if (g_bomb_state == BOMB_PAUSED) {
		// PAUSED: 멈춘 시점의 남은 시간 표시 (도트 ON)
		tm1650_display_time_ds(bomb_ds, 1);

		} else {
		// BOMB_IDLE: 프리셋 설정 모드
		// 선택된 자리만 깜빡 (0.3초 주기)
		if ((int32_t)(now_ms - last_dot_toggle_ms) >= (int32_t)BOMB_IDLE_BLINK_MS) {
			last_dot_toggle_ms += BOMB_IDLE_BLINK_MS;
			dot_on = !dot_on;
		}

		tm1650_display_time_ds_with_blink(
		bomb_preset_ds,
		bomb_selected_digit,
		dot_on
		);
	}

	update_bomb_buzzer(now_ms);
}

// ===============================================================
//                           main
// ===============================================================

int main(void) {
	_delay_ms(10);

	tm1650_init();
	timer0_init();
	led_init();

	sei();

	// 디스플레이 초기값
	tm1650_display_time_ds(0, 1);

	uint8_t last_button_raw = 0; // 이전 TM1650 버튼 raw 값

	while (1) {
		uint32_t now = millis_read();

		// ----- TM1650 버튼 읽기 & 엣지 검출 -----
		int8_t btn = tm1650_get_button_1_to_4();

		if (btn != 0 && last_button_raw == 0) {
			// 0 → 1~4로 올라가는 순간만 클릭으로 인식
			switch (btn) {
				case 1: btn1_clicked = 1; break;
				case 2: btn2_clicked = 1; break;
				case 3: btn3_clicked = 1; break;
				case 4: btn4_clicked = 1; break;
				default: break;
			}
		}
		last_button_raw = (uint8_t)btn;

		// ----- 모드 전환 처리 -----
		handle_mode_button();

		// ----- 각 모드 동작 -----
		if (g_mode == MODE_STOPWATCH) {
			update_stopwatch(now);
			} else {
			update_bomb(now);
		}

		// 부저 펄스 종료 처리
		buzzer_update(now);

		// 버튼 채터링/폴링 주기
		_delay_ms(10);
	}
}

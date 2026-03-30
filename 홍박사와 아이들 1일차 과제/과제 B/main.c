#define F_CPU 8000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdint.h>

// ======================= PIN & KEY =======================
// 핀 매핑
#define SDA_PIN     PB0
#define SCL_PIN     PB2
#define BUZZER_PIN  PB3
#define LED_PIN     PB4

#define SDA_PORT    PORTB
#define SDA_DDR     DDRB
#define SDA_PINREG  PINB

#define SCL_PORT    PORTB
#define SCL_DDR     DDRB

// 키 매핑 (기능키)
#define KEY_ENTER   10
#define KEY_SET     11
#define KEY_RESET   12
#define KEY_BACK    13
#define KEY_NONE    -1

// ======================= I2C (Software) ==================
// TM1650용 소프트웨어 I2C (bit-banging)

static void i2c_delay(void) { _delay_us(5); }

static void SDA_release(void) {
	SDA_DDR &= ~(1 << SDA_PIN);   // 입력(High-Z) + 풀업
	SDA_PORT |= (1 << SDA_PIN);
}

static void SDA_low(void) {
	SDA_DDR |= (1 << SDA_PIN);    // 출력 Low
	SDA_PORT &= ~(1 << SDA_PIN);
}

static void SCL_high(void) {
	SCL_DDR &= ~(1 << SCL_PIN);   // 입력(High-Z) + 풀업
	SCL_PORT |= (1 << SCL_PIN);
	i2c_delay();
}

static void SCL_low(void) {
	SCL_DDR |= (1 << SCL_PIN);    // 출력 Low
	SCL_PORT &= ~(1 << SCL_PIN);
	i2c_delay();
}

static uint8_t SDA_read(void) {
	return (SDA_PINREG & (1 << SDA_PIN)) ? 1 : 0;
}

static void i2c_init(void) {
	SDA_release();
	SCL_high();
}

static void i2c_start(void) {
	SDA_release();
	SCL_high();
	i2c_delay();
	SDA_low();
	i2c_delay();
	SCL_low();
}

static void i2c_stop(void) {
	SDA_low();
	i2c_delay();
	SCL_high();
	i2c_delay();
	SDA_release();
	i2c_delay();
}

static uint8_t i2c_write_byte(uint8_t data) {
	// MSB부터 전송
	for (int8_t i = 7; i >= 0; i--) {
		SCL_low();
		if (data & (1 << i)) SDA_release();
		else SDA_low();
		i2c_delay();
		SCL_high();
		i2c_delay();
	}
	// ACK 입력
	SCL_low();
	SDA_release();
	i2c_delay();
	SCL_high();
	i2c_delay();
	uint8_t ack = !SDA_read();
	SCL_low();
	return ack;
}

static uint8_t i2c_read_byte(uint8_t ack) {
	uint8_t data = 0;
	SDA_release();
	for (int8_t i = 7; i >= 0; i--) {
		SCL_low();
		i2c_delay();
		SCL_high();
		i2c_delay();
		if (SDA_read()) data |= (1 << i);
	}
	// ACK/NACK 전송
	SCL_low();
	if (ack) SDA_low();
	else     SDA_release();
	i2c_delay();
	SCL_high();
	i2c_delay();
	SCL_low();
	SDA_release();
	return data;
}

// ======================= TM1650 & KEYPAD =================
// TM1650 세그먼트 주소
#define TM1650_CTRL_WRITE 0x48
#define TM1650_DIG1_WRITE 0x68
#define TM1650_DIG2_WRITE 0x6A
#define TM1650_DIG3_WRITE 0x6C
#define TM1650_DIG4_WRITE 0x6E

// 0~9 + OFF 세그먼트 패턴
static const uint8_t seg_table[11] = {
	0x3F,0x06,0x5B,0x4F,0x66,
	0x6D,0x7D,0x07,0x7F,0x6F,
	0x00
};
#define SEG_OFF 0x00

// 자리 주소 테이블
static const uint8_t digit_addr[4] = {
	TM1650_DIG1_WRITE, TM1650_DIG2_WRITE,
	TM1650_DIG3_WRITE, TM1650_DIG4_WRITE
};

// 키패드 코드 → 의미 매핑용
typedef struct {
	uint8_t code7;
	int8_t  key_val;
} KeyMap;

// TM1650 키코드 매핑(데이터시트 기준)
static const KeyMap keymap[] = {
	// 1행: 1 2 3 SET
	{ 0x44, 1 }, { 0x4C, 2 }, { 0x54, 3 }, { 0x5C, KEY_SET },
	// 2행: 4 5 6
	{ 0x45, 4 }, { 0x4D, 5 }, { 0x55, 6 },
	// 3행: 7 8 9
	{ 0x46, 7 }, { 0x4E, 8 }, { 0x56, 9 },
	// 4행: ENTER 0 RESET BACK
	{ 0x47, KEY_ENTER }, { 0x4F, 0 }, { 0x57, KEY_RESET }, { 0x5F, KEY_BACK }
};
#define KEYMAP_SIZE (sizeof(keymap)/sizeof(KeyMap))

static void tm1650_write_raw(uint8_t addr, uint8_t pattern) {
	i2c_start();
	i2c_write_byte(addr);
	i2c_write_byte(pattern);
	i2c_stop();
}

// TM1650 디스플레이 설정 초기화
static void tm1650_ctrl_init(void) {
	i2c_start();
	i2c_write_byte(TM1650_CTRL_WRITE);
	i2c_write_byte(0x8F); // Display ON, 최대 밝기
	i2c_stop();
}

static void tm1650_clear_all(void) {
	for (uint8_t i = 0; i < 4; i++) {
		tm1650_write_raw(digit_addr[i], SEG_OFF);
	}
}

// TM1650 키코드 → 내부 키값
static int8_t keycode_to_val(uint8_t code7) {
	for (uint8_t i = 0; i < KEYMAP_SIZE; i++) {
		if (code7 == keymap[i].code7) return keymap[i].key_val;
	}
	return KEY_NONE;
}

// TM1650 키 레지스터 스캔
static int8_t tm1650_scan(void) {
	const uint8_t regs[4] = {0x49, 0x4B, 0x4D, 0x4F};
	for (uint8_t i = 0; i < 4; i++) {
		i2c_start();
		i2c_write_byte(regs[i]);
		uint8_t raw = i2c_read_byte(0);
		i2c_stop();

		uint8_t code7 = raw & 0x7F;
		if (code7 & 0x40) {
			int8_t k = keycode_to_val(code7);
			if (k != KEY_NONE) return k;
		}
	}
	return KEY_NONE;
}

// =================== BUZZER / LED / TIMERS ==============
// 사운드, LED, 잠금 상태 관리

static volatile uint8_t  alarm_active   = 0;
static volatile uint16_t alarm_ms       = 0;

// 잠금: 초 단위 카운트
static volatile uint8_t  lockout_active = 0;
static volatile uint8_t  lockout_sec    = 0;     // 남은 초
static volatile uint16_t lockout_tick   = 0;     // 1ms 누적
static volatile uint8_t  prev_lockout_sec = 0xFF;

// 부저용 토글(100us tick 기준)
static volatile uint8_t  audio_active = 0;
static volatile uint16_t tone_half_us = 1000;
static volatile uint16_t audio_acc_us = 0;

// 기본 톤들
#define TONE_HALF_US_KEY      1000  // 500 Hz
#define TONE_HALF_US_WRONG     900  // ~714 Hz
#define TONE_HALF_US_OK1      1050  // ~600 Hz
#define TONE_HALF_US_OK2       850  // ~800 Hz
#define TONE_HALF_US_OK3       700  // ~1000 Hz

// LED 유지 시간(ms)
static volatile uint16_t led_ms = 0;

// 성공 멜로디 시퀀스
typedef struct {
	uint16_t duration_ms;
	uint16_t half_us;
	uint8_t  audio_on;
} ToneStep;

// "삐- 비--- 빅(열떄)"
static const ToneStep success_melody[] = {
	{ 150, TONE_HALF_US_OK1, 1 },
	{  40, 0,                 0 },
	{ 220, TONE_HALF_US_OK2, 1 },
	{  40, 0,                 0 },
	{ 180, TONE_HALF_US_OK3, 1 }
};
#define SUCCESS_MELODY_LEN (sizeof(success_melody)/sizeof(ToneStep))

static volatile uint8_t melody_active = 0;
static volatile uint8_t melody_index  = 0;

static void buzzer_init(void) {
	DDRB |= (1 << BUZZER_PIN);
	PORTB &= ~(1 << BUZZER_PIN);
}
static void led_init(void) {
	DDRB |= (1 << LED_PIN);
	PORTB &= ~(1 << LED_PIN);
}
static inline void buzzer_off_pin(void) { PORTB &= ~(1<<BUZZER_PIN); }
static inline void led_on(void)  { PORTB |=  (1 << LED_PIN); }
static inline void led_off(void) { PORTB &= ~(1 << LED_PIN); }

// Timer0: 1ms tick (잠금, LED, 알람 관리)
static void timer0_init_1ms(void) {
	TCCR0A = (1 << WGM01);
	TCCR0B = (1 << CS01) | (1 << CS00);
	OCR0A  = 124;
	TIMSK |= (1 << OCIE0A);
}

// Timer1: 100us tick (부저 토글용)
static void timer1_audio_init_100us(void) {
	TCCR1  = 0;
	GTCCR |= (1<<PSR1);
	OCR1C  = 99;
	OCR1A  = 99;
	TIMSK |= (1<<OCIE1A);
	TCCR1  = (1<<CTC1) | (1<<CS11);
}

// 공용 알람 시작
static void trigger_alarm_with_half_period(uint16_t ms, uint16_t half_us) {
	if (lockout_active) return;
	melody_active = 0;

	alarm_ms     = ms;
	alarm_active = 1;
	tone_half_us = half_us;
	audio_acc_us = 0;
	audio_active = 1;
}

static void trigger_alarm(uint16_t ms) {
	trigger_alarm_with_half_period(ms, TONE_HALF_US_KEY);
}

static void trigger_alarm_fail(uint16_t ms) {
	trigger_alarm_with_half_period(ms, TONE_HALF_US_WRONG);
}

// 성공 멜로디 시작
static void start_success_melody(void) {
	if (lockout_active) return;

	melody_active = 1;
	melody_index  = 0;

	const ToneStep *st = &success_melody[0];
	alarm_ms     = st->duration_ms;
	alarm_active = 1;

	if (st->audio_on) {
		audio_active = 1;
		tone_half_us = st->half_us;
		audio_acc_us = 0;
		} else {
		audio_active = 0;
		buzzer_off_pin();
	}
}

// 멜로디 다음 단계
static void melody_next_step(void) {
	if (!melody_active) return;

	melody_index++;
	if (melody_index >= SUCCESS_MELODY_LEN) {
		melody_active = 0;
		alarm_active  = 0;
		audio_active  = 0;
		buzzer_off_pin();
		return;
	}

	const ToneStep *st = &success_melody[melody_index];
	alarm_ms     = st->duration_ms;
	alarm_active = 1;

	if (st->audio_on && !lockout_active) {
		audio_active = 1;
		tone_half_us = st->half_us;
		audio_acc_us = 0;
		} else {
		audio_active = 0;
		buzzer_off_pin();
	}
}

// =================== PASSWORD / STATE ====================
#define MODE_NORMAL 0
#define MODE_SET    1

static uint8_t password[4]  = {0, 0, 0, 0}; // 현재 비밀번호
static uint8_t input_buf[4] = {0, 0, 0, 0}; // 입력 버퍼
static uint8_t input_idx    = 0;
static uint8_t mode         = MODE_NORMAL;   // 일반/설정 모드
static uint8_t failed_attempts = 0;          // 누적 실패 횟수

// =================== SEGMENT HELPERS =====================
static void seg_all_off(void) {
	for (uint8_t i = 0; i < 4; i++) {
		tm1650_write_raw(digit_addr[i], SEG_OFF);
	}
}

// 현재 입력값만 표시
static void seg_show_input_only(void) {
	for (uint8_t i = 0; i < 4; i++) {
		uint8_t pat = (i < input_idx) ? seg_table[input_buf[i]] : SEG_OFF;
		tm1650_write_raw(digit_addr[i], pat);
	}
}

// =================== LOCKOUT & ISRs ======================
// 4회 이상 틀리면 10초 잠금
static void start_lockout(void) {
	lockout_active   = 1;
	lockout_sec      = 10;
	lockout_tick     = 0;
	prev_lockout_sec = 0xFF;

	alarm_active  = 0;
	audio_active  = 0;
	melody_active = 0;
	buzzer_off_pin();
	led_ms = 0;
	led_off();
	seg_all_off();
}

// 부저 토글 인터럽트 (100us 주기)
ISR(TIM1_COMPA_vect) {
	if (!audio_active) {
		buzzer_off_pin();
		audio_acc_us = 0;
		return;
	}
	audio_acc_us += 100;
	if (audio_acc_us >= tone_half_us && tone_half_us != 0) {
		PINB |= (1<<BUZZER_PIN);    // 토글
		audio_acc_us = 0;
	}
}

// 1ms 타이머: 잠금, LED 유지시간, 알람/멜로디 관리
ISR(TIM0_COMPA_vect) {
	if (lockout_active) {
		if (lockout_sec > 0) {
			if (++lockout_tick >= 1000) {
				lockout_tick = 0;
				if (lockout_sec > 0) lockout_sec--;
			}
			} else {
			// 잠금 종료
			lockout_active   = 0;
			alarm_active     = 0;
			audio_active     = 0;
			melody_active    = 0;
			buzzer_off_pin();
			led_ms           = 0;
			led_off();
			prev_lockout_sec = 0xFF;
			failed_attempts  = 0;
			input_idx        = 0;
			seg_all_off();
		}
		return;
	}

	// LED 비블로킹 유지
	if (led_ms > 0) {
		if (--led_ms == 0) {
			led_off();
		}
	}

	// 부저/멜로디 타이밍
	if (alarm_active) {
		if (alarm_ms > 0) {
			alarm_ms--;
			} else {
			alarm_active = 0;
			audio_active = 0;
			buzzer_off_pin();

			if (melody_active) {
				melody_next_step();
			}
		}
		} else {
		if (!melody_active) {
			audio_active = 0;
			buzzer_off_pin();
		}
	}
}

// ================= DISPLAY / EFFECTS =====================
// 입력값 화면 갱신
static void refresh_display(void) {
	if (lockout_active) return;
	seg_show_input_only();
}

// 입력 버퍼 초기화
static void reset_input(void) {
	input_idx = 0;
	refresh_display();
}

// 오답 시: 입력된 숫자들 깜빡
static void blink_wrong_input(void) {
	if (lockout_active) return;
	if (input_idx == 0) return;

	for (uint8_t n = 0; n < 3; n++) {
		seg_show_input_only();
		_delay_ms(120);

		seg_all_off();
		_delay_ms(120);
	}
}

// 정답 시: '0'이 DIG1→DIG4로 달리는 애니메이션
static void success_animation(void) {
	if (lockout_active) return;

	for (uint8_t loop = 0; loop < 2; loop++) {
		for (uint8_t pos = 0; pos < 4; pos++) {
			for (uint8_t i = 0; i < 4; i++) {
				tm1650_write_raw(
				digit_addr[i],
				(i == pos) ? seg_table[0] : SEG_OFF
				);
			}
			_delay_ms(100);
		}
	}

	seg_all_off();
}

// ======================= KEY HANDLER =====================
// 키 입력에 따른 상태 머신
static void handle_key(int key) {
	if (lockout_active) return;

	// RESET 키
	if (key == KEY_RESET) {
		reset_input();
		led_off();
		if (mode == MODE_SET) {
			// 설정 모드에서 RESET → 비번 0000, 일반 모드 복귀
			password[0]=0; password[1]=0; password[2]=0; password[3]=0;
			mode = MODE_NORMAL;
			trigger_alarm(50);
		}
		return;
	}

	// BACK 키: 한 자리 삭제
	if (key == KEY_BACK) {
		if (input_idx > 0) {
			input_idx--;
			refresh_display();
		}
		return;
	}

	// SET 키: 비밀번호 설정 모드 진입
	if (key == KEY_SET) {
		mode = MODE_SET;
		reset_input();
		led_off();
		trigger_alarm(100);
		return;
	}

	// 숫자 입력
	if (key >= 0 && key <= 9) {
		if (input_idx < 4) {
			input_buf[input_idx++] = (uint8_t)key;
			refresh_display();
		}
		return;
	}

	// ENTER 키
	if (key == KEY_ENTER) {
		// 4자리 미만도 '오답' 처리
		if (input_idx != 4) {
			failed_attempts++;
			trigger_alarm_fail(300);
			blink_wrong_input();
			led_off();
			if (failed_attempts > 3) {
				start_lockout();
			}
			reset_input();
			return;
		}

		// 비밀번호 설정 모드: 입력값을 새 비번으로 저장
		if (mode == MODE_SET) {
			for (uint8_t i = 0; i < 4; i++) password[i] = input_buf[i];
			mode = MODE_NORMAL;
			reset_input();
			trigger_alarm(80);
			_delay_ms(120);
			trigger_alarm(80);
			} else {
			// 일반 모드: 비밀번호 검증
			uint8_t match = 1;
			for (uint8_t i = 0; i < 4; i++) {
				if (input_buf[i] != password[i]) match = 0;
			}

			if (match) {
				// 정답
				input_idx = 0;
				failed_attempts = 0;

				// 멜로디 + 애니메이션 + LED
				start_success_melody();
				success_animation();
				led_on();
				led_ms = 1200;

				} else {
				// 오답
				failed_attempts++;
				trigger_alarm_fail(300);
				blink_wrong_input();
				led_off();
				if (failed_attempts > 3) {
					start_lockout();
				}
				reset_input();
			}
		}
	}
}

// ========================== MAIN =========================
int main(void) {
	i2c_init();
	tm1650_ctrl_init();
	seg_all_off();
	
	buzzer_init();
	led_init();
	timer0_init_1ms();
	timer1_audio_init_100us();
	
	sei();

	uint8_t prev_pressed = 0;

	while (1) {
		if (lockout_active) {
			// 잠금 중: DIG3~4에 남은 초 표시
			uint8_t sec = lockout_sec;
			if (sec == 0) sec = 1;

			if (sec != prev_lockout_sec) {
				prev_lockout_sec = sec;

				uint8_t tens = sec / 10;
				uint8_t ones = sec % 10;

				tm1650_write_raw(digit_addr[0], SEG_OFF);
				tm1650_write_raw(digit_addr[1], SEG_OFF);

				if (tens > 0)
				tm1650_write_raw(digit_addr[2], seg_table[tens]);
				else
				tm1650_write_raw(digit_addr[2], SEG_OFF);

				tm1650_write_raw(digit_addr[3], seg_table[ones]);
			}

			} else {
			// 평상시: 키 스캔 + Rising edge에서만 처리
			int8_t key = tm1650_scan();
			uint8_t pressed = (key != KEY_NONE) ? 1 : 0;

			if (!prev_pressed && pressed) {
				trigger_alarm(40);  // 키 입력음
				handle_key(key);
			}

			prev_pressed = pressed;
		}

		_delay_ms(20); // 키 폴링 주기 + 디바운스 느낌
	}
}

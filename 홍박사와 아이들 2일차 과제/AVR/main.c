#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <avr/pgmspace.h>

// ---------------- HW: 두 HUB75(64x32) 세로 스택 -> 가상 64x64 ----------------
#define PANEL_W 64
#define PANEL_H 32
#define CHAIN_N 2
#define SHIFT_W (PANEL_W * CHAIN_N)
#define VIRT_W  64
#define VIRT_H  64

// ---------------- 레이아웃 ----------------
#define FIELD_W 10
#define FIELD_H 32          // 2px * 32row = 64px
#define BLOCK_SZ 2
#define PLAY_X0 12
#define SCORE_X0 40
#define SCORE_Y0 4

// ---------------- 핀 ----------------
#define R1_PD PD2
#define G1_PD PD3
#define B1_PD PD4
#define R2_PD PD5
#define G2_PD PD6
#define B2_PD PD7
#define CLK_PB PB0
#define OE_PB  PB1
#define LAT_PB PB2
#define A_PC   PC0
#define B_PC   PC1
#define C_PC   PC2
#define D_PC   PC3
#define BTN_LEFT_PB  PB3
#define BTN_DOWN_PB  PB4
#define BTN_RIGHT_PC PC4
#define BTN_ROT_PC   PC5

#define CLK_HIGH() (PORTB|=(1<<CLK_PB))
#define CLK_LOW()  (PORTB&=~(1<<CLK_PB))
#define LAT_HIGH() (PORTB|=(1<<LAT_PB))
#define LAT_LOW()  (PORTB&=~(1<<LAT_PB))
#define OE_HIGH()  (PORTB|=(1<<OE_PB))
#define OE_LOW()   (PORTB&=~(1<<OE_PB))

static inline uint8_t btn_left(void){ return !(PINB&(1<<BTN_LEFT_PB)); }
static inline uint8_t btn_down(void){ return !(PINB&(1<<BTN_DOWN_PB)); }
static inline uint8_t btn_right(void){ return !(PINC&(1<<BTN_RIGHT_PC)); }
static inline uint8_t btn_rot(void){  return !(PINC&(1<<BTN_ROT_PC));  }

// ---------------- 색 ----------------
#define RGB6(r,g,b) ((uint8_t)((r)|((g)<<2)|((b)<<4)))
#define COL_BLACK       RGB6(0,0,0)
#define COL_BG1         RGB6(0,1,1)   // 어두운 남청
#define COL_BG2         RGB6(0,0,0)   // 블랙
#define COL_BORDER_DIM  RGB6(0,0,2)
#define COL_BORDER_BRT  RGB6(0,0,3)
#define COL_SCOREBOX    RGB6(1,1,1)
#define COL_I           RGB6(0,3,3)   // 시안
#define COL_O           RGB6(3,3,0)   // 노랑
#define COL_T           RGB6(3,0,3)   // 보라
#define COL_S           RGB6(3,1,1)   // 핑크(초록 금지)
#define COL_Z           RGB6(3,0,0)   // 빨강
#define COL_J           RGB6(3,3,3)   // 흰색(파랑 대신)
#define COL_L           RGB6(3,2,0)   // 주황

// ---------------- 상태 ----------------
static uint8_t field[FIELD_H][FIELD_W];
static int8_t  cur_x, cur_y;
static uint8_t cur_type, cur_rot;
static uint8_t rnd_state = 1;

static uint16_t score = 0;
static uint8_t score_dig[3];

static uint16_t frame_cnt = 0;
static uint8_t  drop_frames = 18;    // 자동 낙하 (조금 빠르게)
#define DROP_FRAMES_MIN 8
#define SOFT_DROP_STEPS 2

static uint8_t game_over = 0;        // GAME/END 화면 트리거(천장 도달)

// 100점마다: 간단 귀여운 보블 춤(12x12)
static uint16_t dance_small_timer = 0;
#define DANCE_SMALL_DURATION 200
#define DANCE_SMALL_X0 44   // 점수판 오른쪽 큰 공간
#define DANCE_SMALL_Y0 18
#define DANCE_SMALL_COL1 RGB6(3,2,0) // 주황
#define DANCE_SMALL_COL2 RGB6(3,0,3) // 자홍

// 300점: 스테이지 클리어 (게임 중지 + 축하 영상 + 큰 텍스트)
static uint8_t  stage_clear_active = 0;
static uint16_t stage_clear_timer  = 0;
#define STAGE_CLEAR_DURATION 360

// ---------------- 폰트 ----------------
const uint8_t digit_font[10][5] PROGMEM = {
	{0x07,0x05,0x05,0x05,0x07},{0x02,0x06,0x02,0x02,0x07},{0x07,0x01,0x07,0x04,0x07},
	{0x07,0x01,0x07,0x01,0x07},{0x05,0x05,0x07,0x01,0x01},{0x07,0x04,0x07,0x01,0x07},
	{0x07,0x04,0x07,0x05,0x07},{0x07,0x01,0x01,0x01,0x01},{0x07,0x05,0x07,0x05,0x07},{0x07,0x05,0x07,0x01,0x07}
};

// 5x7 대문자 (S,T,A,G,E,C,L,R,N,D 필요)
enum {CH_SPC=0, CH_A, CH_C, CH_D, CH_E, CH_G, CH_L, CH_N, CH_R, CH_S, CH_T};
const uint8_t font5x7[][7] PROGMEM = {
	{0,0,0,0,0,0,0},                       // space
	{0x1C,0x22,0x22,0x3E,0x22,0x22,0x22}, // A
	{0x3C,0x22,0x22,0x3C,0x22,0x22,0x3C}, // C
	{0x3C,0x22,0x22,0x22,0x22,0x22,0x3C}, // D
	{0x3E,0x20,0x20,0x3C,0x20,0x20,0x3E}, // E
	{0x1E,0x20,0x20,0x2E,0x22,0x22,0x1E}, // G
	{0x22,0x22,0x22,0x22,0x22,0x22,0x3E}, // L
	{0x22,0x32,0x2A,0x26,0x22,0x22,0x22}, // N
	{0x3C,0x22,0x22,0x3C,0x28,0x24,0x22}, // R
	{0x1E,0x20,0x20,0x1C,0x02,0x02,0x3C}, // S
	{0x3E,0x08,0x08,0x08,0x08,0x08,0x08}  // T
};
static inline uint8_t fidx(char c){
	switch(c){
		case 'A':return CH_A; case 'C':return CH_C; case 'D':return CH_D; case 'E':return CH_E;
		case 'G':return CH_G; case 'L':return CH_L; case 'N':return CH_N; case 'R':return CH_R;
		case 'S':return CH_S; case 'T':return CH_T; default: return CH_SPC;
	}
}

// ---------------- 테트로미노(4x4) ----------------
const uint8_t tetromino[7][4][4] PROGMEM = {
	{{0x00,0x00,0x00,0x0F},{0x02,0x02,0x02,0x02},{0x00,0x00,0x00,0x0F},{0x02,0x02,0x02,0x02}},
	{{0x00,0x06,0x06,0x00},{0x00,0x06,0x06,0x00},{0x00,0x06,0x06,0x00},{0x00,0x06,0x06,0x00}},
	{{0x00,0x0E,0x04,0x00},{0x04,0x06,0x04,0x00},{0x04,0x0E,0x00,0x00},{0x04,0x0C,0x04,0x00}},
	{{0x00,0x06,0x0C,0x00},{0x08,0x0C,0x04,0x00},{0x00,0x06,0x0C,0x00},{0x08,0x0C,0x04,0x00}},
	{{0x00,0x0C,0x06,0x00},{0x04,0x0C,0x08,0x00},{0x00,0x0C,0x06,0x00},{0x04,0x0C,0x08,0x00}},
	{{0x00,0x0E,0x02,0x00},{0x06,0x04,0x04,0x00},{0x08,0x0E,0x00,0x00},{0x04,0x04,0x0C,0x00}},
	{{0x00,0x0E,0x08,0x00},{0x04,0x04,0x06,0x00},{0x02,0x0E,0x00,0x00},{0x0C,0x04,0x04,0x00}}
};

// ---------------- RNG ----------------
static uint8_t rand8(void){
	uint8_t l=rnd_state;
	l = (l&1)?((l>>1)^0xB8):(l>>1);
	rnd_state=l; return l;
}

// ---------------- 유틸 ----------------
static uint8_t block_color(uint8_t t){
	switch(t){
		case 1:return COL_I; case 2:return COL_O; case 3:return COL_T;
		case 4:return COL_S; case 5:return COL_Z; case 6:return COL_J;
		case 7:return COL_L; default:return COL_BLACK;
	}
}
static void update_score_digits(void){
	uint16_t s=score; if(s>999)s=999;
	score_dig[2]=s%10; s/=10;
	score_dig[1]=s%10; s/=10;
	score_dig[0]=s%10;
}
static void clear_field(void){
	for(uint8_t y=0;y<FIELD_H;y++) for(uint8_t x=0;x<FIELD_W;x++) field[y][x]=0;
	score=0; update_score_digits();
	game_over=0; stage_clear_active=0; stage_clear_timer=0; dance_small_timer=0;
}

// ---------------- 조각 로직 ----------------
static uint8_t can_move(int8_t dx,int8_t dy,int8_t drot){
	uint8_t nr=(cur_rot+drot)&3;
	for(uint8_t ly=0;ly<4;ly++){
		uint8_t row=pgm_read_byte(&tetromino[cur_type][nr][ly]);
		if(!row) continue;
		for(uint8_t lx=0;lx<4;lx++){
			if(!(row&(1<<(3-lx)))) continue;
			int16_t nx=(int16_t)cur_x+dx+lx;
			int16_t ny=(int16_t)cur_y+dy+ly;
			if(nx<0||nx>=FIELD_W) return 0;
			if(ny<0) continue;
			if(ny>=FIELD_H) return 0;
			if(field[ny][nx]) return 0;
		}
	}
	return 1;
}
static void lock_and_clear(void){
	for(uint8_t ly=0;ly<4;ly++){
		uint8_t row=pgm_read_byte(&tetromino[cur_type][cur_rot][ly]);
		if(!row) continue;
		for(uint8_t lx=0;lx<4;lx++){
			if(!(row&(1<<(3-lx)))) continue;
			int16_t gx=(int16_t)cur_x+lx;
			int16_t gy=(int16_t)cur_y+ly;
			if(gx<0||gx>=FIELD_W||gy<0||gy>=FIELD_H) continue;
			field[gy][gx]=cur_type+1;
		}
	}
	uint8_t lines=0;
	for(int8_t y=FIELD_H-1;y>=0;y--){
		uint8_t full=1; for(uint8_t x=0;x<FIELD_W;x++){ if(!field[y][x]){full=0;break;} }
		if(full){
			for(int8_t yy=y;yy>0;yy--) for(uint8_t x=0;x<FIELD_W;x++) field[yy][x]=field[yy-1][x];
			for(uint8_t x=0;x<FIELD_W;x++) field[0][x]=0;
			lines++; y++;
		}
	}
	if(lines){
		uint16_t prev = score;
		score += (uint16_t)lines*100; if(score>999) score=999;
		update_score_digits();
		if((prev/100)<(score/100)) dance_small_timer=DANCE_SMALL_DURATION;
	}
}
static uint8_t spawn_piece(void){
	cur_type=rand8()%7; cur_rot=0; cur_x=3; cur_y=0;
	return !can_move(0,0,0); // 못 놓으면 천장 도달
}
static void game_step(void){
	if(stage_clear_active) return;
	if(game_over) return;
	if(can_move(0,1,0)) cur_y++;
	else{
		lock_and_clear();
		if(spawn_piece()) game_over=1; // 천장 도달 → GAME/END
	}
}

// ---------------- 텍스트(5x7) 2배 스케일 렌더 ----------------
static inline void overlay_text_5x7_s2(uint8_t vy,uint8_t vx,
uint8_t x0,uint8_t y0,
const char* s,uint8_t color,
uint8_t *c_out){
	// 문자 폭 5, 높이 7, 스케일2 => 폭10, 높이14, 간격2(px)
	if(vy<y0 || vx<x0) return;
	uint8_t ry = vy - y0;
	uint8_t rx = vx - x0;
	uint8_t char_w = 12;  // 10(스케일2) + 2(간격)
	uint8_t idx = rx / char_w;
	uint8_t inx = rx % char_w;
	if(!s[idx]) return;
	if(inx>=10 || ry>=14) return;
	uint8_t cell_x = inx/2;  // 0..4
	uint8_t cell_y = ry/2;   // 0..6
	uint8_t bits = pgm_read_byte(&font5x7[fidx(s[idx])][cell_y]);
	if(bits & (1<<(4-cell_x))) *c_out = color;
}

// ---------------- 100점: 보블 춤(12x12, 2프레임) ----------------
static const uint16_t blob1[12] PROGMEM = {
	0x03C0,0x07E0,0x0FF0,0x1FF8,0x1FF8,0x1FF8,0x1FF8,0x1DF8,0x0FF0,0x07E0,0x03C0,0x0180
};
static const uint16_t blob2[12] PROGMEM = {
	0x0180,0x03C0,0x07E0,0x0FF0,0x1DF8,0x1FF8,0x1FF8,0x1FF8,0x0FF0,0x07E0,0x03C0,0x0180
};
static inline void overlay_dance_small(uint8_t vy,uint8_t vx,uint8_t *c,uint16_t tick){
	if(!dance_small_timer) return;
	if(vx<DANCE_SMALL_X0 || vx>=DANCE_SMALL_X0+12 || vy<DANCE_SMALL_Y0 || vy>=DANCE_SMALL_Y0+12) return;
	uint8_t px=vx-DANCE_SMALL_X0, py=vy-DANCE_SMALL_Y0;
	const uint16_t* f = ((tick>>2)&1)? blob2:blob1;
	uint16_t row = pgm_read_word(&f[py]);
	if(row & (1<<(15-px))) *c = (((tick>>2)&1)? DANCE_SMALL_COL2:DANCE_SMALL_COL1);
}

// ---------------- 300점: STAGE CLEAR(게임 정지+축하) ----------------
static inline void overlay_stage_clear(uint8_t vy,uint8_t vx,uint8_t *c,uint16_t tick){
	if(!stage_clear_active) return;

	// 배경: 회전하는 대각선 패턴 (레인보우 톤)
	uint8_t stripe = ((vx + vy + (tick&31)) & 7);
	const uint8_t pal[4] = { RGB6(3,3,0), RGB6(3,0,3), RGB6(0,3,3), RGB6(3,0,0) };
	*c = pal[stripe & 3];

	// 상단 "STAGE" / 하단 "CLEAR" (크게, 중앙정렬)
	overlay_text_5x7_s2(vy,vx,  6,10, "STAGE", RGB6(3,3,3), c); // top
	overlay_text_5x7_s2(vy,vx,  6,40, "CLEAR", RGB6(3,3,3), c); // bottom
}

// ---------------- GAME/END + 실망 애니(12x12) ----------------
static const uint16_t sad1[12] PROGMEM = {
	0x03C0,0x0420,0x0810,0x0810,0x0000,0x0000,0x0000,0x0000,0x0810,0x0660,0x0000,0x0000
};
static const uint16_t sad2[12] PROGMEM = {
	0x03C0,0x0420,0x0810,0x0810,0x0000,0x0000,0x0000,0x0000,0x0660,0x0810,0x0000,0x0000
};
static inline void overlay_game_end(uint8_t vy,uint8_t vx,uint8_t *c,uint16_t tick){
	if(!game_over || stage_clear_active) return;

	// 텍스트
	overlay_text_5x7_s2(vy,vx, 10,10,"GAME", RGB6(3,3,3), c); // top panel
	overlay_text_5x7_s2(vy,vx, 16,42,"END",  RGB6(3,3,3), c); // bottom panel

	// 가운데 시무룩(12x12)
	uint8_t cx=26, cy=26;
	if(vx>=cx && vx<cx+12 && vy>=cy && vy<cy+12){
		uint8_t px=vx-cx, py=vy-cy;
		const uint16_t* f = ((tick>>3)&1)? sad2:sad1;
		uint16_t row = pgm_read_word(&f[py]);
		if(row & (1<<(15-px))) *c = RGB6(3,0,0);
	}
}

// ---------------- 픽셀 계산 ----------------
static inline void get_pixel_color_virt(uint8_t vy,uint8_t vx,
uint8_t *r,uint8_t *g,uint8_t *b,
uint16_t tick)
{
	// 스테이지 클리어면 전체 오버레이 먼저
	uint8_t c = (((vx>>1)+(vy>>1))&1)? COL_BG1:COL_BG2;
	if(stage_clear_active){
		overlay_stage_clear(vy,vx,&c,tick);
		*r=c&3; *g=(c>>2)&3; *b=(c>>4)&3; return;
	}

	// 테두리
	uint8_t pw = FIELD_W*BLOCK_SZ;
	uint8_t ix0=PLAY_X0, ix1=PLAY_X0+pw-1;
	uint8_t bx0 = (ix0>=2)? ix0-2:0, bx1=ix1+2;
	if((vx==bx0||vx==bx0+1||vx==bx1||vx==bx1-1))
	c=((vx==bx0||vx==bx1)? COL_BORDER_BRT:COL_BORDER_DIM);
	if(vy==0||vy==VIRT_H-1){
		if(vx>=bx0&&vx<=bx1) c=((vy==0)?COL_BORDER_BRT:COL_BORDER_DIM);
	}

	// 필드
	if(vx>=ix0 && vx<ix0+pw){
		uint8_t col=(vx-ix0)>>1;
		uint8_t cy = vy>>1;
		if(cy<FIELD_H){
			uint8_t t=field[cy][col];
			if((int16_t)col>=(int16_t)cur_x && (int16_t)col<(int16_t)(cur_x+4) &&
			(int16_t)cy>=(int16_t)cur_y && (int16_t)cy<(int16_t)(cur_y+4)){
				int16_t lx=(int16_t)col-(int16_t)cur_x;
				int16_t ly=(int16_t)cy -(int16_t)cur_y;
				if(lx>=0&&lx<4&&ly>=0&&ly<4){
					uint8_t row=pgm_read_byte(&tetromino[cur_type][cur_rot][(uint8_t)ly]);
					if(row&(1<<(3-(uint8_t)lx))) t=cur_type+1;
				}
			}
			if(t) c=block_color(t);
			else  c=(((col+cy)&1)==0)? RGB6(0,0,1):RGB6(0,0,0);
		}
	}

	// 점수판
	if(vx>=SCORE_X0-2 && vx<=SCORE_X0+18 && vy>=SCORE_Y0-2 && vy<=SCORE_Y0+10){
		if(vx==SCORE_X0-2||vx==SCORE_X0+18||vy==SCORE_Y0-2||vy==SCORE_Y0+10) c=COL_BORDER_BRT;
		else if(vx==SCORE_X0-1||vx==SCORE_X0+17||vy==SCORE_Y0-1||vy==SCORE_Y0+9) c=COL_BORDER_DIM;
		else c=COL_SCOREBOX;
	}
	// 점수(빨강)
	if(vx>=SCORE_X0 && vx<SCORE_X0+12 && vy>=SCORE_Y0 && vy<SCORE_Y0+5){
		uint8_t dx=vx-SCORE_X0, di=dx/4, px=dx%4;
		if(di<3 && px<3){
			uint8_t dy=vy-SCORE_Y0;
			uint8_t d=score_dig[di];
			uint8_t bits=pgm_read_byte(&digit_font[d][dy]);
			if(bits & (1<<(2-px))) c=RGB6(3,0,0);
		}
	}

	// 100점 보블 춤
	overlay_dance_small(vy,vx,&c,tick);

	// GAME/END + 실망 애니
	overlay_game_end(vy,vx,&c,tick);

	*r=c&3; *g=(c>>2)&3; *b=(c>>4)&3;
}

// ---------------- HUB75 스캔(두 패널) ----------------
static inline void set_row(uint8_t row){ uint8_t v=PORTC&0xF0; v|=(row&0x0F); PORTC=v; }
static inline void shift_rowpair(uint8_t rowpair,uint8_t bit,uint16_t tick){
	for(uint8_t x=0;x<SHIFT_W;x++){
		uint8_t r1,g1,b1,r2,g2,b2;
		uint8_t chain=(x>=PANEL_W)?1:0;
		uint8_t vx=(x%PANEL_W);
		uint8_t vy_top    = rowpair + (chain?32:0);
		uint8_t vy_bottom = rowpair + (chain?48:16);
		get_pixel_color_virt(vy_top,   vx,&r1,&g1,&b1,tick);
		get_pixel_color_virt(vy_bottom,vx,&r2,&g2,&b2,tick);

		uint8_t v=PORTD&0x03;
		v&=~((1<<R1_PD)|(1<<G1_PD)|(1<<B1_PD)|(1<<R2_PD)|(1<<G2_PD)|(1<<B2_PD));
		if((r1>>bit)&1) v|=(1<<R1_PD);
		if((g1>>bit)&1) v|=(1<<G1_PD);
		if((b1>>bit)&1) v|=(1<<B1_PD);
		if((r2>>bit)&1) v|=(1<<R2_PD);
		if((g2>>bit)&1) v|=(1<<G2_PD);
		if((b2>>bit)&1) v|=(1<<B2_PD);
		PORTD=v;
		CLK_HIGH(); _delay_us(1); CLK_LOW();
	}
}
static void scan_frame(uint16_t tick){
	const uint8_t base_T_us=40;
	for(uint8_t bit=0;bit<2;bit++){
		uint8_t on=(1<<bit)*base_T_us;
		for(uint8_t row=0;row<16;row++){
			set_row(row);
			shift_rowpair(row,bit,tick);
			LAT_HIGH(); _delay_us(1); LAT_LOW();
			OE_LOW(); for(uint8_t t=0;t<on;t++) _delay_us(1); OE_HIGH();
		}
	}
}

// ---------------- 입력(오토리핏) ----------------
typedef struct{ uint8_t prev,hold; } BtnState;
static BtnState stL={0,0},stR={0,0},stD={0,0},stRot={0,0};
#define REPEAT_DELAY 6
#define REPEAT_RATE  2
static uint8_t edge_or_rep(uint8_t now, BtnState* st){
	uint8_t fire=0;
	if(now){
		if(!st->prev){ fire=1; st->hold=0; }
		else{
			st->hold++;
			if(st->hold>REPEAT_DELAY && ((st->hold-REPEAT_DELAY)%REPEAT_RATE==0)) fire=1;
		}
	}else st->hold=0;
	st->prev=now; return fire;
}

// ---------------- 메인 ----------------
static void update_speed_from_score(void){
	int16_t t=18-(int16_t)(score/50);
	if(t<DROP_FRAMES_MIN) t=DROP_FRAMES_MIN;
	drop_frames=(uint8_t)t;
}

int main(void){
	DDRD|=(1<<R1_PD)|(1<<G1_PD)|(1<<B1_PD)|(1<<R2_PD)|(1<<G2_PD)|(1<<B2_PD);
	DDRB|=(1<<CLK_PB)|(1<<OE_PB)|(1<<LAT_PB);
	DDRC|=(1<<A_PC)|(1<<B_PC)|(1<<C_PC)|(1<<D_PC);
	DDRB&=~((1<<BTN_LEFT_PB)|(1<<BTN_DOWN_PB)); PORTB|=((1<<BTN_LEFT_PB)|(1<<BTN_DOWN_PB));
	DDRC&=~((1<<BTN_RIGHT_PC)|(1<<BTN_ROT_PC)); PORTC|=((1<<BTN_RIGHT_PC)|(1<<BTN_ROT_PC));
	CLK_LOW(); LAT_LOW(); OE_HIGH();

	uint16_t tick=0;
	clear_field();
	if(spawn_piece()) game_over=1;

	while(1){
		scan_frame(tick); tick++;

		// 300점 도달 시 게임 정지 + 축하 시작 (한 번만)
		if(!stage_clear_active && score>=300){
			stage_clear_active=1;
			stage_clear_timer = STAGE_CLEAR_DURATION;
		}

		if(stage_clear_active){
			if(stage_clear_timer) stage_clear_timer--;
			// 끝나면 아무 버튼으로 재시작
			if(!stage_clear_timer){
				if(btn_left()||btn_right()||btn_down()||btn_rot()){
					clear_field(); update_speed_from_score();
					if(spawn_piece()) game_over=1;
				}
			}
			continue; // 게임 로직 정지
		}

		if(dance_small_timer) dance_small_timer--;

		uint8_t l=btn_left(), r=btn_right(), d=btn_down(), rt=btn_rot();

		if(game_over){
			if(l||r||d||rt){
				clear_field(); update_speed_from_score();
				if(spawn_piece()) game_over=1;
			}
			continue; // GAME/END 유지
		}

		if(edge_or_rep(l,&stL)){ if(can_move(-1,0,0)) cur_x--; }
		if(edge_or_rep(r,&stR)){ if(can_move(+1,0,0)) cur_x++; }
		if(edge_or_rep(rt,&stRot)){ if(can_move(0,0,+1)) cur_rot=(cur_rot+1)&3; }
		if(edge_or_rep(d,&stD)){
			for(uint8_t i=0;i<SOFT_DROP_STEPS;i++){ if(can_move(0,+1,0)) cur_y++; else break; }
		}

		frame_cnt++;
		if(frame_cnt>=drop_frames){
			frame_cnt=0;
			game_step();
			if(!game_over){
				if(!can_move(0,0,0)){ if(spawn_piece()) game_over=1; }
			}
		}
	}
	return 0;
}

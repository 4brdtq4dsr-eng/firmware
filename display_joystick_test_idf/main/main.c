#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_HOST SPI2_HOST

#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK 12
#define PIN_NUM_CS 10
#define PIN_NUM_DC 9
#define PIN_NUM_RST 14

#define LCD_H_RES 170
#define LCD_V_RES 320

#define JOY_LEFT 6
#define JOY_RIGHT 7
#define JOY_UP 4
#define JOY_DOWN 5
#define JOY_MID 16

#define DEBOUNCE_MS 40
#define LOOP_MS 10

static const char *TAG = "disp_joy_test";

typedef struct {
    int x;
    int y;
} offset_preset_t;

static const offset_preset_t k_offsets[] = {
    {0, 0},
    {35, 0},
    {0, 80},
};

typedef struct {
    gpio_num_t pin;
    const char *name;
    int raw_level;
    int stable_level;
    int64_t last_change_ms;
} button_t;

static button_t g_buttons[] = {
    {.pin = JOY_LEFT, .name = "LEFT"},
    {.pin = JOY_RIGHT, .name = "RIGHT"},
    {.pin = JOY_UP, .name = "UP"},
    {.pin = JOY_DOWN, .name = "DOWN"},
    {.pin = JOY_MID, .name = "MID"},
};

static esp_lcd_panel_handle_t g_panel;
static int g_offset_idx = 1;
static bool g_running = true;
static bool g_theme_red = false;
static int64_t g_accumulated_ms = 0;
static int64_t g_start_ms = 0;
static char g_prev_time[16] = "";

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline uint16_t fg_color(void) {
    return g_theme_red ? rgb565(255, 0, 0) : rgb565(255, 255, 255);
}

static const uint8_t *glyph_for_codepoint(uint32_t cp) {
    static const uint8_t g_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t g_A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t g_E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t g_K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t g_N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t g_P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t g_S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t g_T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t g_X[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t g_Y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t g_COLON[7] = {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00};
    static const uint8_t g_EQ[7] = {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00};
    static const uint8_t g_0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t g_1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t g_2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t g_3[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t g_5[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};

    switch (cp) {
        case ' ': return g_space;
        case 'X': return g_X;
        case 'Y': return g_Y;
        case ':': return g_COLON;
        case '=': return g_EQ;
        case '0': return g_0;
        case '1': return g_1;
        case '2': return g_2;
        case '3': return g_3;
        case '5': return g_5;
        case 0x0422: return g_T; // Т
        case 0x0415: return g_E; // Е
        case 0x0421: return g_S; // С
        case 0x041A: return g_K; // К
        case 0x0420: return g_P; // Р
        case 0x0410: return g_A; // А
        case 0x041D: return g_N; // Н
        default: return g_space;
    }
}

static void lcd_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > LCD_H_RES) {
        w = LCD_H_RES - x;
    }
    if (y + h > LCD_V_RES) {
        h = LCD_V_RES - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    static uint16_t line[LCD_H_RES];
    for (int i = 0; i < w; ++i) {
        line[i] = color;
    }
    for (int row = 0; row < h; ++row) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel, x, y + row, x + w, y + row + 1, line));
    }
}

static void lcd_draw_glyph5x7_scaled(int x, int y, const uint8_t glyph[7], uint16_t color, int scale) {
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if ((glyph[row] >> (4 - col)) & 1) {
                lcd_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static uint32_t utf8_next(const char **s) {
    const uint8_t *p = (const uint8_t *)(*s);
    if (*p < 0x80) {
        (*s)++;
        return *p;
    }
    if ((*p & 0xE0) == 0xC0) {
        uint32_t cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
        (*s) += 2;
        return cp;
    }
    (*s)++;
    return ' ';
}

static void lcd_draw_text5x7(int x, int y, const char *text, uint16_t color, int scale) {
    const char *p = text;
    int cursor = x;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        const uint8_t *glyph = glyph_for_codepoint(cp);
        lcd_draw_glyph5x7_scaled(cursor, y, glyph, color, scale);
        cursor += 6 * scale;
    }
}

static void draw_seg_h(int x, int y, int len, int thick, uint16_t color) {
    lcd_fill_rect(x, y, len, thick, color);
}

static void draw_seg_v(int x, int y, int len, int thick, uint16_t color) {
    lcd_fill_rect(x, y, thick, len, color);
}

static void draw_digit7seg(int x, int y, int digit, uint16_t color, int scale) {
    static const uint8_t map[10] = {
        0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
        0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011,
    };
    int thick = 4 * scale;
    int len = 18 * scale;
    int h = 20 * scale;

    uint8_t m = map[digit % 10];
    if (m & 0b1000000) draw_seg_h(x + thick, y, len, thick, color);                           // a
    if (m & 0b0100000) draw_seg_v(x + len + thick, y + thick, h, thick, color);              // b
    if (m & 0b0010000) draw_seg_v(x + len + thick, y + h + 2 * thick, h, thick, color);      // c
    if (m & 0b0001000) draw_seg_h(x + thick, y + 2 * h + 2 * thick, len, thick, color);      // d
    if (m & 0b0000100) draw_seg_v(x, y + h + 2 * thick, h, thick, color);                    // e
    if (m & 0b0000010) draw_seg_v(x, y + thick, h, thick, color);                             // f
    if (m & 0b0000001) draw_seg_h(x + thick, y + h + thick, len, thick, color);              // g
}

static void draw_colon(int x, int y, uint16_t color, int scale) {
    int s = 5 * scale;
    lcd_fill_rect(x, y + 20 * scale, s, s, color);
    lcd_fill_rect(x, y + 48 * scale, s, s, color);
}

static void draw_dot(int x, int y, uint16_t color, int scale) {
    int s = 5 * scale;
    lcd_fill_rect(x, y + 60 * scale, s, s, color);
}

static void format_time(char *out, size_t len, int64_t total_ms) {
    int tenths = (int)((total_ms / 100) % 10);
    int seconds = (int)((total_ms / 1000) % 60);
    int minutes = (int)((total_ms / 60000) % 100);
    snprintf(out, len, "%02d:%02d.%d", minutes, seconds, tenths);
}

static void draw_time(const char *t) {
    const int x0 = 6;
    const int y0 = 92;
    const int scale = 1;
    lcd_fill_rect(0, 85, LCD_H_RES, 110, rgb565(0, 0, 0));

    uint16_t color = fg_color();
    int x = x0;
    for (size_t i = 0; i < strlen(t); ++i) {
        char c = t[i];
        if (c >= '0' && c <= '9') {
            draw_digit7seg(x, y0, c - '0', color, scale);
            x += 30;
        } else if (c == ':') {
            draw_colon(x, y0, color, scale);
            x += 12;
        } else if (c == '.') {
            draw_dot(x, y0, color, scale);
            x += 12;
        }
    }
}

static void apply_offset(void) {
    const offset_preset_t p = k_offsets[g_offset_idx];
    esp_lcd_panel_set_gap(g_panel, p.x, p.y);
    ESP_LOGI(TAG, "СМЕЩЕНИЕ: X=%d Y=%d (preset %d)", p.x, p.y, g_offset_idx);
}

static void draw_static_ui(void) {
    lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, rgb565(0, 0, 0));
    lcd_draw_text5x7(4, 8, "ТЕСТ ЭКРАНА", fg_color(), 2);

    char line[48];
    snprintf(line, sizeof(line), "СМЕЩЕНИЕ: X=%d Y=%d", k_offsets[g_offset_idx].x, k_offsets[g_offset_idx].y);
    lcd_draw_text5x7(4, 32, line, fg_color(), 2);

    lcd_draw_text5x7(4, 210, "MID: START/STOP", fg_color(), 2);
    lcd_draw_text5x7(4, 230, "UP: RESET", fg_color(), 2);
    lcd_draw_text5x7(4, 250, "DOWN: OFFSET", fg_color(), 2);
    lcd_draw_text5x7(4, 270, "LEFT/RIGHT: THEME", fg_color(), 2);
}

static void init_buttons(void) {
    for (size_t i = 0; i < sizeof(g_buttons) / sizeof(g_buttons[0]); ++i) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << g_buttons[i].pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        int level = gpio_get_level(g_buttons[i].pin);
        g_buttons[i].raw_level = level;
        g_buttons[i].stable_level = level;
        g_buttons[i].last_change_ms = esp_timer_get_time() / 1000;
    }
    ESP_LOGI(TAG, "Joystick GPIO configured as INPUT_PULLUP (pressed=LOW)");
}

static void handle_press(const char *name) {
    if (strcmp(name, "MID") == 0) {
        if (g_running) {
            g_accumulated_ms += (esp_timer_get_time() / 1000) - g_start_ms;
            g_running = false;
            ESP_LOGI(TAG, "MID -> stopwatch STOP");
        } else {
            g_start_ms = esp_timer_get_time() / 1000;
            g_running = true;
            ESP_LOGI(TAG, "MID -> stopwatch START");
        }
    } else if (strcmp(name, "UP") == 0) {
        g_accumulated_ms = 0;
        g_start_ms = esp_timer_get_time() / 1000;
        ESP_LOGI(TAG, "UP -> reset stopwatch to 00:00.0");
    } else if (strcmp(name, "DOWN") == 0) {
        g_offset_idx = (g_offset_idx + 1) % 3;
        apply_offset();
        draw_static_ui();
        g_prev_time[0] = '\0';
        ESP_LOGI(TAG, "DOWN -> cycle offset preset");
    } else if (strcmp(name, "LEFT") == 0) {
        g_theme_red = false;
        draw_static_ui();
        g_prev_time[0] = '\0';
        ESP_LOGI(TAG, "LEFT -> Theme 1 (white)");
    } else if (strcmp(name, "RIGHT") == 0) {
        g_theme_red = true;
        draw_static_ui();
        g_prev_time[0] = '\0';
        ESP_LOGI(TAG, "RIGHT -> Theme 2 (red)");
    }
}

static void poll_buttons(void) {
    int64_t now_ms = esp_timer_get_time() / 1000;
    for (size_t i = 0; i < sizeof(g_buttons) / sizeof(g_buttons[0]); ++i) {
        button_t *b = &g_buttons[i];
        int level = gpio_get_level(b->pin);

        if (level != b->raw_level) {
            b->raw_level = level;
            b->last_change_ms = now_ms;
        }

        if ((now_ms - b->last_change_ms) >= DEBOUNCE_MS && b->stable_level != b->raw_level) {
            b->stable_level = b->raw_level;
            if (b->stable_level == 0) {
                ESP_LOGI(TAG, "%s pressed", b->name);
                handle_press(b->name);
            }
        }
    }
}

static void init_display(void) {
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_CLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &g_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(g_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(g_panel, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_panel, true));

    apply_offset();
    ESP_LOGI(TAG, "Display initialized: ST7789 170x320, SPI SCK=GPIO12 MOSI=GPIO11 CS=GPIO10 DC=GPIO9 RST=GPIO14");
}

void app_main(void) {
    ESP_LOGI(TAG, "Booting display+joystick test firmware");
    ESP_LOGI(TAG, "ТЕСТ ЭКРАНА");

    init_display();
    init_buttons();

    draw_static_ui();
    vTaskDelay(pdMS_TO_TICKS(1200));

    g_start_ms = esp_timer_get_time() / 1000;
    int64_t last_update = 0;

    while (1) {
        poll_buttons();

        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_update >= 100) {
            last_update = now;
            int64_t elapsed = g_accumulated_ms;
            if (g_running) {
                elapsed += now - g_start_ms;
            }

            char time_buf[16];
            format_time(time_buf, sizeof(time_buf), elapsed);
            if (strcmp(time_buf, g_prev_time) != 0) {
                draw_time(time_buf);
                strncpy(g_prev_time, time_buf, sizeof(g_prev_time) - 1);
                g_prev_time[sizeof(g_prev_time) - 1] = '\0';
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

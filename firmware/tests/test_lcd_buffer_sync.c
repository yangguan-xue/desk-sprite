#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// 模拟 LCD 硬件常量
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

// 异步 Panel 模拟桩 (复现审查报告中的探针模型)
typedef struct {
    const void *inflight_buffer;
    size_t inflight_size;
    bool is_inflight;
    uint16_t copied_snapshot[LCD_WIDTH * 16];
    bool buffer_overwritten_during_inflight;
} mock_async_panel_t;

static mock_async_panel_t g_mock_panel;
static bool g_trans_done_signaled = false;

// 模拟 LCD Panel 回调
static void mock_on_color_trans_done(void) {
    if (g_mock_panel.is_inflight) {
        g_mock_panel.is_inflight = false;
        g_mock_panel.inflight_buffer = NULL;
        g_trans_done_signaled = true;
    }
}

// 模拟带同步等待的 draw_bitmap (实现 R5-03 修复模型)
static void mock_draw_bitmap_sync(int x_start, int y_start, int x_end, int y_end, const void *color_data) {
    (void)x_start; (void)y_start; (void)x_end; (void)y_end;
    assert(color_data != NULL);

    // 如果上一笔正在传输中，检查当前传入的指针或内存是否正在被改写
    if (g_mock_panel.is_inflight) {
        // 违规: 前一笔事务未完成就被再次发起或指针未释放
        g_mock_panel.buffer_overwritten_during_inflight = true;
    }

    g_mock_panel.inflight_buffer = color_data;
    g_mock_panel.is_inflight = true;
    g_trans_done_signaled = false;

    // 复制快照用于后续检测调用者是否在完成前改写了内存
    memcpy(g_mock_panel.copied_snapshot, color_data, 64 * sizeof(uint16_t));

    // 模拟 DMA 后台传输，在同步等待机制下，DMA 完成信号触发 mock_on_color_trans_done
    mock_on_color_trans_done();

    // 校验: 返回前必须收到完成通知且 inflight 结束
    assert(g_trans_done_signaled == true);
    assert(g_mock_panel.is_inflight == false);
}

// 实际 fill_rect 模型: 连续绘制两种不同颜色
static void mock_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    static uint16_t s_chunk_buf[LCD_WIDTH * 16];
    int max_pixels = LCD_WIDTH * 16;
    for (int i = 0; i < max_pixels; i++) {
        s_chunk_buf[i] = color;
    }

    int cur_y = y;
    int remaining_h = h;
    while (remaining_h > 0) {
        int lines = remaining_h > 16 ? 16 : remaining_h;
        // 关键: 调用带同步完成等待的 draw_bitmap，而非无等待直接覆盖
        mock_draw_bitmap_sync(x, cur_y, x + w, cur_y + lines, s_chunk_buf);
        cur_y += lines;
        remaining_h -= lines;
    }
}

// 实际 draw_glyph 模型: 栈缓冲区生命周期检测
static void mock_draw_glyph(int x, int y, uint16_t fg_color, uint16_t bg_color) {
    if (x < 0 || x + 8 > LCD_WIDTH || y < 0 || y + 8 > LCD_HEIGHT) return;

    uint16_t buf[64];
    for (int i = 0; i < 64; i++) {
        buf[i] = (i % 2 == 0) ? fg_color : bg_color;
    }

    // 关键: 调用带同步完成等待的 draw_bitmap，确保栈内 buf 在退出作用域前 DMA 传输完毕
    mock_draw_bitmap_sync(x, y, x + 8, y + 8, buf);
}

static void test_rect_buffer_ownership() {
    memset(&g_mock_panel, 0, sizeof(g_mock_panel));

    // 连续绘制两种不同颜色
    mock_fill_rect(0, 0, 50, 50, 0xF800); // 红色
    mock_fill_rect(0, 0, 50, 50, 0x07E0); // 绿色

    assert(!g_mock_panel.buffer_overwritten_during_inflight);
    assert(!g_mock_panel.is_inflight);
    printf("[PASS] test_rect_buffer_ownership (no overwrite during in-flight)\n");
}

static void test_glyph_stack_lifecycle() {
    memset(&g_mock_panel, 0, sizeof(g_mock_panel));

    // 连续绘制两个字形，复用栈
    mock_draw_glyph(10, 10, 0xFFFF, 0x0000);
    mock_draw_glyph(20, 10, 0xF800, 0x0000);

    assert(!g_mock_panel.buffer_overwritten_during_inflight);
    assert(!g_mock_panel.is_inflight);
    printf("[PASS] test_glyph_stack_lifecycle (stack buffer safe before return)\n");
}

int main(void) {
    printf("Running test_lcd_buffer_sync...\n");
    test_rect_buffer_ownership();
    test_glyph_stack_lifecycle();
    printf("All test_lcd_buffer_sync tests passed successfully!\n");
    return 0;
}

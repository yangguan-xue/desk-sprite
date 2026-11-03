#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "sprite_types.h"
#include "ui_layout.h"
#include "ui_metrics.h"
#include "alert_filter.h"
#include "alert_queue.h"
#include "action_reporter.h"
#include "touch_button.h"
#include "sim_mqtt.h"
#include "sim_audio.h"

#ifdef HAS_SDL2
#include <SDL.h>
#endif

static volatile bool g_running = true;
static alert_scheduler_t g_alert_sched;
static work_mode_t g_current_mode = WORK_MODE_IDLE;

static metrics_payload_t g_last_metrics;
static ui_metrics_render_model_t g_metrics_render_model;
static bool g_has_metrics = false;

static char g_last_played_event_id[64] = {0};

static void sig_int_handler(int sig) {
    (void)sig;
    g_running = false;
}

static void on_alert(const alert_payload_t* alert) {
    printf("[Sim Main] MQTT Alert received: %s [%s] -> %s\n",
           alert->title,
           alert->priority == PRIORITY_HIGH ? "HIGH" : (alert->priority == PRIORITY_MEDIUM ? "MED" : "LOW"),
           alert->summary);

    // 接入阶段 1 入队过滤与优先级调度 (R06)
    int pushed = alert_scheduler_push(&g_alert_sched, alert, g_current_mode);
    if (pushed == 0) {
        printf("[Sim Main] Alert admitted to scheduler queue.\n");
    } else {
        printf("[Sim Main] Alert filtered/dropped by mode %d or queue policy.\n", g_current_mode);
    }
}

static void on_metrics(const metrics_payload_t* metrics) {
    printf("[Sim Main] Metrics received: count=%d, ttl=%d, sampled_at=%lld\n",
           metrics->item_count, metrics->ttl_sec, metrics->sampled_at);
    g_last_metrics = *metrics;
    g_has_metrics = true;
    ui_metrics_compute_render_model(&g_last_metrics, (long long)time(NULL), &g_metrics_render_model);
}

static void on_state(work_mode_t mode) {
    const char* mode_names[] = {"IDLE", "CODING", "MEETING", "SLEEP"};
    printf("[Sim Main] State mode updated: %s\n", mode_names[mode % 4]);
    g_current_mode = mode;
    ui_layout_update_status_bar(true, true, mode, "18:30");

    // 出队复核与模式协同 (SPEC-003): 若切入 SLEEP，清空等待中的全部 Medium 告警
    if (mode == WORK_MODE_SLEEP) {
        alert_scheduler_clear_medium(&g_alert_sched);
    }
}

int main(int argc, char* argv[]) {
    bool headless = false;
    bool run_once = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "-h") == 0) {
            headless = true;
        }
        if (strcmp(argv[i], "--once") == 0) {
            run_once = true;
        }
    }

    signal(SIGINT, sig_int_handler);
    signal(SIGTERM, sig_int_handler);

    printf("===========================================\n");
    printf("   Desk Sprite SDL2/CLI Simulator v1.4.0   \n");
    printf("===========================================\n");

    ui_layout_init();
    sim_audio_init();
    alert_scheduler_init(&g_alert_sched);

    // 默认初始看板数据
    memset(&g_last_metrics, 0, sizeof(g_last_metrics));
    g_last_metrics.item_count = 3;
    g_last_metrics.ttl_sec = 3600;
    g_last_metrics.sampled_at = (long long)time(NULL);

    strncpy(g_last_metrics.items[0].key, "kimi_token", sizeof(g_last_metrics.items[0].key));
    strncpy(g_last_metrics.items[0].label, "Kimi 5H", sizeof(g_last_metrics.items[0].label));
    g_last_metrics.items[0].current = 42000.0;
    g_last_metrics.items[0].total = 100000.0;
    strncpy(g_last_metrics.items[0].unit, "tok", sizeof(g_last_metrics.items[0].unit));
    g_last_metrics.items[0].direction = METRIC_DIR_USED;
    g_last_metrics.items[0].status = METRIC_STATUS_NORMAL;

    strncpy(g_last_metrics.items[1].key, "deepseek_cost", sizeof(g_last_metrics.items[1].key));
    strncpy(g_last_metrics.items[1].label, "DeepSeek月度", sizeof(g_last_metrics.items[1].label));
    g_last_metrics.items[1].current = 28.5;
    g_last_metrics.items[1].total = 100.0;
    strncpy(g_last_metrics.items[1].unit, "¥", sizeof(g_last_metrics.items[1].unit));
    g_last_metrics.items[1].direction = METRIC_DIR_USED;
    g_last_metrics.items[1].status = METRIC_STATUS_NORMAL;

    strncpy(g_last_metrics.items[2].key, "copilot_sub", sizeof(g_last_metrics.items[2].key));
    strncpy(g_last_metrics.items[2].label, "Copilot订阅", sizeof(g_last_metrics.items[2].label));
    g_last_metrics.items[2].current = 18.0;
    g_last_metrics.items[2].total = 30.0;
    strncpy(g_last_metrics.items[2].unit, "天", sizeof(g_last_metrics.items[2].unit));
    g_last_metrics.items[2].direction = METRIC_DIR_REMAINING;
    g_last_metrics.items[2].status = METRIC_STATUS_NORMAL;

    ui_metrics_compute_render_model(&g_last_metrics, (long long)time(NULL), &g_metrics_render_model);
    g_has_metrics = true;

    sim_mqtt_config_t mqtt_cfg = {
        .broker_host = "127.0.0.1",
        .broker_port = 1883,
        .device_id = "esp32_c049ef",
        .username = "sprite_device",
        .password = "device_secret",
        .on_alert = on_alert,
        .on_metrics = on_metrics,
        .on_state = on_state,
    };
    sim_mqtt_init(&mqtt_cfg);
    sim_mqtt_publish_status("online");

#ifdef HAS_SDL2
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;

    if (!headless) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            headless = true;
        } else {
            window = SDL_CreateWindow(
                "Desk Sprite (240x240)",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                240, 240, SDL_WINDOW_SHOWN
            );
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        }
    }
#else
    headless = true;
#endif

    printf("Simulator running (%s). Press Ctrl+C to exit.\n", headless ? "Headless Mode" : "SDL2 GUI Mode");

    int tick_count = 0;
    time_t last_sec = time(NULL);

    while (g_running) {
        sim_mqtt_loop(50);

        long long now = (long long)time(NULL);

        // 每秒驱动一次调度器与倒计时
        if (now != last_sec) {
            last_sec = now;
            alert_scheduler_tick(&g_alert_sched, now, g_current_mode);

            // 定期重新计算 metrics stale 状态
            if (g_has_metrics) {
                ui_metrics_compute_render_model(&g_last_metrics, now, &g_metrics_render_model);
            }
        }

        // 检查当前是否有正在活跃展示的告警
        const alert_payload_t* active_alert = alert_scheduler_get_active(&g_alert_sched);
        if (active_alert) {
            ui_layout_show_alert(active_alert);
            // 若为新弹出的告警，播放提示音
            if (strcmp(g_last_played_event_id, active_alert->event_id) != 0) {
                strncpy(g_last_played_event_id, active_alert->event_id, sizeof(g_last_played_event_id) - 1);
                sim_audio_play(active_alert->sound, active_alert->force_mute);
            }
        } else {
            g_last_played_event_id[0] = '\0';
        }

#ifdef HAS_SDL2
        if (!headless && window) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    g_running = false;
                } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                    // 点击交互: 活跃告警时上报并解除弹窗，主屏时上报普通 tap (R05)
                    device_action_t act;
                    memset(&act, 0, sizeof(act));
                    const char* alert_id = active_alert ? active_alert->event_id : NULL;

                    action_reporter_build_action("esp32_c049ef", GESTURE_TAP, alert_id, now, &act);
                    sim_mqtt_publish_action(&act);

                    if (active_alert) {
                        printf("[Sim Input] Active alert %s dismissed via Tap\n", alert_id);
                        alert_scheduler_dismiss(&g_alert_sched, now, g_current_mode);
                    } else {
                        printf("[Sim Input] Screen tapped in dashboard mode\n");
                    }
                }
            }

            // 绘制基础背景
            SDL_SetRenderDrawColor(renderer, 18, 18, 22, 255);
            SDL_RenderClear(renderer);

            // 1. 顶部状态栏 (高度 24px)
            SDL_SetRenderDrawColor(renderer, 28, 30, 38, 255);
            SDL_Rect top_bar = {0, 0, 240, 24};
            SDL_RenderFillRect(renderer, &top_bar);

            // Wi-Fi 信号指示灯 (绿色)
            SDL_SetRenderDrawColor(renderer, 16, 185, 129, 255);
            SDL_Rect wifi_dot = {8, 8, 8, 8};
            SDL_RenderFillRect(renderer, &wifi_dot);

            // 模式指示灯: IDLE 绿, CODING 蓝, MEETING 黄, SLEEP 灰
            if (g_current_mode == WORK_MODE_CODING) {
                SDL_SetRenderDrawColor(renderer, 59, 130, 246, 255);
            } else if (g_current_mode == WORK_MODE_MEETING) {
                SDL_SetRenderDrawColor(renderer, 245, 158, 11, 255);
            } else if (g_current_mode == WORK_MODE_SLEEP) {
                SDL_SetRenderDrawColor(renderer, 107, 114, 128, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 16, 185, 129, 255);
            }
            SDL_Rect mode_dot = {22, 8, 8, 8};
            SDL_RenderFillRect(renderer, &mode_dot);

            // 2. 舞台区域 (y: 24~167)
            if (active_alert) {
                // 告警弹窗覆盖层
                if (active_alert->priority == PRIORITY_HIGH) {
                    SDL_SetRenderDrawColor(renderer, 220, 38, 38, 255); // 红色
                } else {
                    SDL_SetRenderDrawColor(renderer, 217, 119, 6, 255); // 橙色
                }
                SDL_Rect alert_box = {16, 32, 208, 128};
                SDL_RenderFillRect(renderer, &alert_box);
            } else {
                // 正常桌宠动画区
                SDL_SetRenderDrawColor(renderer, 30, 32, 42, 255);
                SDL_Rect pet_box = {56, 32, 128, 128};
                SDL_RenderFillRect(renderer, &pet_box);
                SDL_SetRenderDrawColor(renderer, 99, 102, 241, 255);
                SDL_RenderDrawRect(renderer, &pet_box);
            }

            // 3. 底部三行度量指标看板 (y: 168~239, R09 动态与 Stale 渲染)
            SDL_SetRenderDrawColor(renderer, 26, 28, 35, 255);
            SDL_Rect metrics_bg = {0, 168, 240, 72};
            SDL_RenderFillRect(renderer, &metrics_bg);

            for (int i = 0; i < 3; i++) {
                int y_pos = 172 + i * 24;
                SDL_SetRenderDrawColor(renderer, 48, 52, 65, 255);
                SDL_Rect r_track = {12, y_pos, 216, 14};
                SDL_RenderFillRect(renderer, &r_track);

                if (i < g_metrics_render_model.count) {
                    const ui_metric_row_render_t* row = &g_metrics_render_model.rows[i];
                    int bar_w = (int)(row->progress_ratio * 216.0f);
                    if (bar_w > 216) bar_w = 216;
                    if (bar_w < 0) bar_w = 0;

                    if (row->style == UI_METRIC_STYLE_STALE) {
                        // Stale 灰显 (R09)
                        SDL_SetRenderDrawColor(renderer, 107, 114, 128, 255);
                    } else if (i == 0) {
                        SDL_SetRenderDrawColor(renderer, 16, 185, 129, 255);
                    } else if (i == 1) {
                        SDL_SetRenderDrawColor(renderer, 59, 130, 246, 255);
                    } else {
                        SDL_SetRenderDrawColor(renderer, 168, 85, 247, 255);
                    }
                    SDL_Rect r_bar = {12, y_pos, bar_w, 14};
                    SDL_RenderFillRect(renderer, &r_bar);
                }
            }

            SDL_RenderPresent(renderer);
        }
#endif

        usleep(50000); // 50ms tick
        tick_count++;
        if (run_once && tick_count >= 5) {
            break;
        }
    }

    printf("Exiting simulator...\n");
    sim_mqtt_publish_status("offline");
    sim_mqtt_destroy();

#ifdef HAS_SDL2
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
#endif

    return 0;
}

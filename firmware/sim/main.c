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
#include "font_utf8.h"

static void sim_draw_glyph(SDL_Renderer* renderer, int x, int y, uint32_t cp, SDL_Color color, int scale) {
    const uint8_t* glyph = sim_get_glyph(cp);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                if (scale == 1) {
                    SDL_RenderDrawPoint(renderer, x + col, y + row);
                } else {
                    SDL_Rect pixel = {x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
    }
}

static void sim_draw_string_wrapped(SDL_Renderer* renderer, int x, int y, int max_w, int max_h, const char* str, SDL_Color color, int scale) {
    if (!str) return;
    int cur_x = x;
    int cur_y = y;
    int line_height = 10 * scale;
    int char_w = 8 * scale;

    const char* ptr = str;
    while (*ptr) {
        if (*ptr == '\n') {
            cur_x = x;
            cur_y += line_height;
            if (cur_y + 8 * scale > y + max_h) break;
            ptr++;
            continue;
        }
        if (cur_x + char_w > x + max_w) {
            // 自动换行 (R3-05)
            cur_x = x;
            cur_y += line_height;
            if (cur_y + 8 * scale > y + max_h) break;
        }
        uint32_t cp = utf8_next_codepoint(&ptr);
        if (cp == 0) break;
        sim_draw_glyph(renderer, cur_x, cur_y, cp, color, scale);
        cur_x += char_w;
    }
}

static void sim_draw_string(SDL_Renderer* renderer, int x, int y, const char* str, SDL_Color color, int scale) {
    sim_draw_string_wrapped(renderer, x, y, 240 - x - 4, 240 - y, str, color, scale);
}
#endif

static volatile bool g_running = true;
static alert_scheduler_t g_alert_sched;
static work_mode_t g_current_mode = WORK_MODE_IDLE;
static ui_metric_view_mode_t g_metric_view_mode = UI_METRIC_VIEW_VALUE;
static long long g_mute_until_sec = 0;
static touch_button_state_t g_touch_button;

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
    if (pushed >= 2) {
        printf("[Sim Main] Alert admitted to scheduler queue.\n");
    } else if (pushed == 1) {
        printf("[Sim Main] Alert admitted as status dot.\n");
    } else {
        printf("[Sim Main] Alert filtered/dropped by mode %d or queue policy.\n", g_current_mode);
    }
}

static void on_metrics(const metrics_payload_t* metrics) {
    printf("[Sim Main] Metrics received: count=%d, ttl=%d, sampled_at=%lld\n",
           metrics->item_count, metrics->ttl_sec, metrics->sampled_at);
    g_last_metrics = *metrics;
    g_has_metrics = true;
    ui_metrics_compute_render_model_ex(&g_last_metrics, (long long)time(NULL), g_metric_view_mode, &g_metrics_render_model);
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

static void on_dismiss(void) {
    long long now = (long long)time(NULL);
    alert_scheduler_dismiss(&g_alert_sched, now, g_current_mode);
    printf("[Sim Main] Active alert dismissed\n");
}

int main(int argc, char* argv[]) {
    // 强制无缓冲标准输出，确保作为子进程执行时管道即时刷新 (R3-06)
    setvbuf(stdout, NULL, _IONBF, 0);

    bool headless = false;
    bool run_once = false;
    bool standalone = false;
    bool strict_mqtt = false;
    bool tls_enabled = false;

    const char* broker_host = getenv("MQTT_HOST") ? getenv("MQTT_HOST") : "127.0.0.1";
    int broker_port = getenv("MQTT_PORT") ? atoi(getenv("MQTT_PORT")) : 1883;
    const char* device_id = getenv("DEVICE_ID") ? getenv("DEVICE_ID") : "esp32_c049ef";
    const char* username = getenv("MQTT_USER");
    const char* password = getenv("MQTT_PASSWORD");
    const char* ca_cert = getenv("MQTT_CA_CERT");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "-h") == 0) {
            headless = true;
        } else if (strcmp(argv[i], "--once") == 0) {
            run_once = true;
        } else if (strcmp(argv[i], "--standalone") == 0) {
            standalone = true;
        } else if (strcmp(argv[i], "--strict-mqtt") == 0) {
            strict_mqtt = true;
        } else if (strcmp(argv[i], "--tls") == 0) {
            tls_enabled = true;
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char* m = argv[++i];
            if (strcmp(m, "coding") == 0) g_current_mode = WORK_MODE_CODING;
            else if (strcmp(m, "meeting") == 0) g_current_mode = WORK_MODE_MEETING;
            else if (strcmp(m, "sleep") == 0) g_current_mode = WORK_MODE_SLEEP;
            else g_current_mode = WORK_MODE_IDLE;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            broker_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            broker_host = argv[++i];
        } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device_id = argv[++i];
        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            username = argv[++i];
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            password = argv[++i];
        } else if (strcmp(argv[i], "--ca") == 0 && i + 1 < argc) {
            ca_cert = argv[++i];
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
    touch_button_init(&g_touch_button);

    // 默认初始看板数据 (Copilot 18/30 60% remaining, RR07)
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
    g_last_metrics.items[0].is_mock = false;

    strncpy(g_last_metrics.items[1].key, "deepseek_cost", sizeof(g_last_metrics.items[1].key));
    strncpy(g_last_metrics.items[1].label, "DeepSeek", sizeof(g_last_metrics.items[1].label));
    g_last_metrics.items[1].current = 28.5;
    g_last_metrics.items[1].total = 100.0;
    strncpy(g_last_metrics.items[1].unit, "¥", sizeof(g_last_metrics.items[1].unit));
    g_last_metrics.items[1].direction = METRIC_DIR_USED;
    g_last_metrics.items[1].status = METRIC_STATUS_NORMAL;
    g_last_metrics.items[1].is_mock = false;

    strncpy(g_last_metrics.items[2].key, "copilot_sub", sizeof(g_last_metrics.items[2].key));
    strncpy(g_last_metrics.items[2].label, "Copilot", sizeof(g_last_metrics.items[2].label));
    g_last_metrics.items[2].current = 18.0;
    g_last_metrics.items[2].total = 30.0;
    strncpy(g_last_metrics.items[2].unit, "天", sizeof(g_last_metrics.items[2].unit));
    g_last_metrics.items[2].direction = METRIC_DIR_REMAINING;
    g_last_metrics.items[2].status = METRIC_STATUS_NORMAL;
    g_last_metrics.items[2].is_mock = true;

    ui_metrics_compute_render_model_ex(&g_last_metrics, (long long)time(NULL), g_metric_view_mode, &g_metrics_render_model);
    g_has_metrics = true;

    if (!standalone) {
        sim_mqtt_config_t mqtt_cfg = {
            .broker_host = broker_host,
            .broker_port = broker_port,
            .device_id = device_id,
            .username = username,
            .password = password,
            .ca_cert = ca_cert,
            .tls_enabled = tls_enabled,
            .strict_mqtt = strict_mqtt,
            .on_alert = on_alert,
            .on_metrics = on_metrics,
            .on_state = on_state,
            .on_dismiss = on_dismiss,
        };
        int m_rc = sim_mqtt_init(&mqtt_cfg);
        if (m_rc != 0) {
            fprintf(stderr, "[Sim Main] Warning: MQTT initialization failed for broker %s:%d\n",
                    broker_host, broker_port);
            if (strict_mqtt) {
                fprintf(stderr, "[Sim Main] Strict MQTT requested, exiting with error code 1\n");
                return 1;
            }
        } else {
            sim_mqtt_publish_status("online");
        }
    } else {
        printf("[Sim Main] Standalone mode: MQTT disabled\n");
    }

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
        if (!standalone) {
            sim_mqtt_loop(50);
            if (strict_mqtt && !sim_mqtt_is_connected()) {
                fprintf(stderr, "[Sim Main] Strict MQTT disconnected, exiting with code 1\n");
                return 1;
            }
        }

        long long now = (long long)time(NULL);

        // 每秒驱动一次调度器与倒计时
        if (now != last_sec) {
            last_sec = now;
            alert_scheduler_tick(&g_alert_sched, now, g_current_mode);

            // 定期重新计算 metrics stale 状态
            if (g_has_metrics) {
                ui_metrics_compute_render_model_ex(&g_last_metrics, now, g_metric_view_mode, &g_metrics_render_model);
            }
        }

        // 检查当前是否有正在活跃展示的告警
        const alert_payload_t* active_alert = alert_scheduler_get_active(&g_alert_sched);
        if (active_alert) {
            ui_layout_show_alert(active_alert);
            // 若为新弹出的告警，播放提示音 (R3-03: 临时静音期间屏蔽 medium 音效)
            if (strcmp(g_last_played_event_id, active_alert->event_id) != 0) {
                printf("[Sim UI] Alert popup rendered on screen: %s [%s]\n",
                       active_alert->title,
                       active_alert->priority == PRIORITY_HIGH ? "HIGH" : (active_alert->priority == PRIORITY_MEDIUM ? "MED" : "LOW"));
                strncpy(g_last_played_event_id, active_alert->event_id, sizeof(g_last_played_event_id) - 1);
                bool mute = active_alert->force_mute;
                if (!mute && active_alert->priority == PRIORITY_MEDIUM && now < g_mute_until_sec) {
                    printf("[Sim Audio] Temporary mute active (remaining %llds), muting medium alert sound\n", g_mute_until_sec - now);
                    mute = true;
                }
                sim_audio_play(active_alert->sound, mute);
            }
        } else {
            g_last_played_event_id[0] = '\0';
        }

#ifdef HAS_SDL2
        static bool s_mouse_is_down = false;
        if (!headless && window) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    g_running = false;
                } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                    s_mouse_is_down = true;
                } else if (event.type == SDL_MOUSEBUTTONUP) {
                    s_mouse_is_down = false;
                }
            }

            long long now_ms = (long long)SDL_GetTicks();
            touch_gesture_t gesture = touch_button_update(&g_touch_button, s_mouse_is_down, now_ms);
            const char* alert_id = active_alert ? active_alert->event_id : NULL;

            if (gesture == GESTURE_TAP) {
                printf("[Sim Input] GESTURE_TAP detected via touch_button\n");
                if (active_alert) {
                    device_action_t act;
                    action_reporter_build_action(device_id, GESTURE_TAP, alert_id, now, &act);
                    if (!standalone) sim_mqtt_publish_action(&act);
                    printf("[Sim Input] Active alert %s interacted & dismissed via Tap\n", alert_id);
                    alert_scheduler_dismiss(&g_alert_sched, now, g_current_mode);
                } else {
                    g_metric_view_mode = (g_metric_view_mode == UI_METRIC_VIEW_VALUE) ? UI_METRIC_VIEW_PERCENT : UI_METRIC_VIEW_VALUE;
                    printf("[Sim Input] Dashboard tapped: toggle metric view mode to %s\n",
                           (g_metric_view_mode == UI_METRIC_VIEW_PERCENT) ? "PERCENT" : "VALUE");
                    ui_metrics_compute_render_model_ex(&g_last_metrics, now, g_metric_view_mode, &g_metrics_render_model);
                }
            } else if (gesture == GESTURE_LONG_PRESS) {
                printf("[Sim Input] GESTURE_LONG_PRESS detected via touch_button\n");
                if (active_alert) {
                    // 静默消除告警: 规范要求严禁向云端上报 open_url 动作报文
                    printf("[Sim Input] Active alert %s silently dismissed via Long Press (no action sent)\n", alert_id);
                    alert_scheduler_dismiss(&g_alert_sched, now, g_current_mode);
                } else {
                    if (now < g_mute_until_sec) {
                        g_mute_until_sec = 0;
                        printf("[Sim Input] Dashboard long press: temporary mute disabled\n");
                    } else {
                        g_mute_until_sec = now + 1800;
                        printf("[Sim Input] Dashboard long press: temporary mute enabled for 30 min (until %lld)\n", g_mute_until_sec);
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

            // 模式指示灯与文字: IDLE 绿, CODING 蓝, MEETING 黄, SLEEP 灰 (RR08)
            const char* mode_tag = "IDLE";
            if (g_current_mode == WORK_MODE_CODING) {
                SDL_SetRenderDrawColor(renderer, 59, 130, 246, 255);
                mode_tag = "CODING";
            } else if (g_current_mode == WORK_MODE_MEETING) {
                SDL_SetRenderDrawColor(renderer, 245, 158, 11, 255);
                mode_tag = "MEETING";
            } else if (g_current_mode == WORK_MODE_SLEEP) {
                SDL_SetRenderDrawColor(renderer, 107, 114, 128, 255);
                mode_tag = "SLEEP";
            } else {
                SDL_SetRenderDrawColor(renderer, 16, 185, 129, 255);
                mode_tag = "IDLE";
            }
            SDL_Rect mode_dot = {22, 8, 8, 8};
            SDL_RenderFillRect(renderer, &mode_dot);

            SDL_Color white_color = {255, 255, 255, 255};
            SDL_Color gray_color = {156, 163, 175, 255};
            sim_draw_string(renderer, 36, 8, mode_tag, gray_color, 1);

            if (now < g_mute_until_sec) {
                SDL_Color yellow_col = {245, 158, 11, 255};
                sim_draw_string(renderer, 150, 8, "[MUTE]", yellow_col, 1);
            }

            // 2. 舞台区域 (y: 24~167)
            if (active_alert) {
                // 告警弹窗覆盖层 (RR08: 实际渲染可读文本、倒计时与优先级)
                if (active_alert->priority == PRIORITY_HIGH) {
                    SDL_SetRenderDrawColor(renderer, 185, 28, 28, 255); // 深红
                } else {
                    SDL_SetRenderDrawColor(renderer, 180, 83, 9, 255); // 深橙
                }
                SDL_Rect alert_box = {12, 30, 216, 132};
                SDL_RenderFillRect(renderer, &alert_box);

                // 绘制标题
                sim_draw_string(renderer, 20, 38, active_alert->title, white_color, 1);

                // 绘制摘要 (最多两行，自动换行, R3-05)
                sim_draw_string_wrapped(renderer, 20, 52, 200, 24, active_alert->summary, white_color, 1);

                // 绘制倒计时与交互提示 (R3-07, SPEC-003 第 3 节)
                if (active_alert->priority == PRIORITY_HIGH) {
                    // High 优先级必须手动确认，无倒计时自动关闭
                    sim_draw_string(renderer, 20, 78, "Action: Manual Ack Required", white_color, 1);
                } else {
                    // Medium 优先级显示调度器剩余展示时长 (duration_sec 倒计时)
                    char cd_buf[32];
                    int rem_sec = alert_scheduler_get_remaining_sec(&g_alert_sched);
                    if (rem_sec < 0) rem_sec = 0;
                    snprintf(cd_buf, sizeof(cd_buf), "Duration: %ds", rem_sec);
                    sim_draw_string(renderer, 20, 78, cd_buf, gray_color, 1);
                }

                // 交互提示栏
                sim_draw_string(renderer, 20, 142, "Tap:Open | Hold:Dismiss", white_color, 1);
            } else {
                // 正常桌宠动画区
                SDL_SetRenderDrawColor(renderer, 30, 32, 42, 255);
                SDL_Rect pet_box = {56, 32, 128, 128};
                SDL_RenderFillRect(renderer, &pet_box);
                SDL_SetRenderDrawColor(renderer, 99, 102, 241, 255);
                SDL_RenderDrawRect(renderer, &pet_box);
                sim_draw_string(renderer, 90, 88, "( ^_^ )", white_color, 1);
            }

            // 3. 底部三行度量指标看板 (y: 168~239, RR07, RR08 完整文本、比例与 [M]/[STALE] 标记)
            SDL_SetRenderDrawColor(renderer, 26, 28, 35, 255);
            SDL_Rect metrics_bg = {0, 168, 240, 72};
            SDL_RenderFillRect(renderer, &metrics_bg);

            for (int i = 0; i < 3; i++) {
                int y_pos = 172 + i * 23;
                SDL_SetRenderDrawColor(renderer, 48, 52, 65, 255);
                SDL_Rect r_track = {10, y_pos, 220, 18};
                SDL_RenderFillRect(renderer, &r_track);

                if (i < g_metrics_render_model.count) {
                    const ui_metric_row_render_t* row = &g_metrics_render_model.rows[i];
                    int bar_w = (int)(row->progress_ratio * 220.0f);
                    if (bar_w > 220) bar_w = 220;
                    if (bar_w < 0) bar_w = 0;

                    if (row->style == UI_METRIC_STYLE_STALE) {
                        SDL_SetRenderDrawColor(renderer, 107, 114, 128, 180); // Stale 灰显
                    } else if (i == 0) {
                        SDL_SetRenderDrawColor(renderer, 16, 185, 129, 200); // 绿色
                    } else if (i == 1) {
                        SDL_SetRenderDrawColor(renderer, 59, 130, 246, 200); // 蓝色
                    } else {
                        SDL_SetRenderDrawColor(renderer, 168, 85, 247, 200); // 紫色
                    }
                    SDL_Rect r_bar = {10, y_pos, bar_w, 18};
                    SDL_RenderFillRect(renderer, &r_bar);

                    // 绘制指标文本 (RR08)
                    SDL_Color text_col = (row->style == UI_METRIC_STYLE_STALE) ? gray_color : white_color;
                    sim_draw_string(renderer, 14, y_pos + 5, row->rendered_text, text_col, 1);
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
    if (!standalone) {
        sim_mqtt_publish_status("offline");
        sim_mqtt_destroy();
    }

#ifdef HAS_SDL2
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
#endif

    return 0;
}

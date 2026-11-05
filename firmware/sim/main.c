#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "sprite_types.h"
#include "ui_layout.h"
#include "ui_metrics.h"
#include "ui_sakura.h"
#include "alert_filter.h"
#include "alert_queue.h"
#include "action_reporter.h"
#include "touch_button.h"
#include "sim_mqtt.h"
#include "sim_audio.h"

#ifdef HAS_SDL2
#include <SDL.h>
static void flush_sakura_sdl(void *ctx, int y, int height, const uint16_t *pixels) {
    SDL_Texture *texture = ctx;
    SDL_Rect area = {0, y, SAKURA_WIDTH, height};
    SDL_UpdateTexture(texture, &area, pixels, SAKURA_WIDTH * sizeof(uint16_t));
}

#endif

static volatile bool g_running = true;
static alert_scheduler_t g_alert_sched;
static work_mode_t g_current_mode = WORK_MODE_IDLE;
static ui_metric_view_mode_t g_metric_view_mode = UI_METRIC_VIEW_PERCENT;
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

    // Real sessions wait for MQTT. Explicit standalone previews carry a mock badge.
    memset(&g_last_metrics, 0, sizeof(g_last_metrics));
    if (standalone) {
        g_last_metrics.item_count = 1;
        g_last_metrics.sampled_at = (long long)time(NULL);
        g_last_metrics.ttl_sec = 3600;
        metric_item_t *demo = &g_last_metrics.items[0];
        snprintf(demo->key, sizeof(demo->key), "quota");
        snprintf(demo->label, sizeof(demo->label), "Remaining");
        demo->current = 68;
        demo->total = 100;
        demo->direction = METRIC_DIR_REMAINING;
        demo->is_mock = true;
        g_has_metrics = true;
        ui_metrics_compute_render_model_ex(&g_last_metrics, (long long)time(NULL), g_metric_view_mode, &g_metrics_render_model);
    }

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
    SDL_Texture* texture = NULL;
    uint16_t strip[SAKURA_BUFFER_PIXELS];
    uint32_t last_frame = 0;

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
            if (window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
            if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            if (renderer) texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                SDL_TEXTUREACCESS_STREAMING, SAKURA_WIDTH, SAKURA_HEIGHT);
            if (!texture) {
                fprintf(stderr, "SDL display unavailable: %s\n", SDL_GetError());
                headless = true;
            }
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
            uint32_t frame_ms = SDL_GetTicks();
            if (frame_ms - last_frame >= 125 || last_frame == 0) {
                char clock_text[8] = "--:--";
                time_t clock_now = (time_t)now;
                struct tm *local = localtime(&clock_now);
                if (now > 1700000000 && local) strftime(clock_text, sizeof(clock_text), "%H:%M", local);
                sakura_scene_t scene = {
                    .mode = g_current_mode, .clock = clock_text,
                    .wifi = sim_mqtt_is_connected(), .mqtt = sim_mqtt_is_connected(),
                    .muted = now < g_mute_until_sec || g_current_mode == WORK_MODE_MEETING,
                    .touch_enabled = true,
                    .show_value = g_metric_view_mode == UI_METRIC_VIEW_VALUE,
                    .elapsed_ms = frame_ms, .now = now,
                    .metrics = g_has_metrics ? &g_last_metrics : NULL, .alert = active_alert,
                    .alert_remaining_sec = alert_scheduler_get_remaining_sec(&g_alert_sched),
                };
                ui_sakura_render(&scene, strip, flush_sakura_sdl, texture);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                last_frame = frame_ms - ((frame_ms - last_frame) % 125);
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
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
#endif

    return 0;
}

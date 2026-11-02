#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "sprite_types.h"
#include "ui_layout.h"
#include "sim_mqtt.h"
#include "sim_audio.h"

#ifdef HAS_SDL2
#include <SDL2/SDL.h>
#endif

static volatile bool g_running = true;

static void sig_int_handler(int sig) {
    (void)sig;
    g_running = false;
}

static void on_alert(const alert_payload_t* alert) {
    printf("[Sim Main] Alert received: %s [%s] -> %s\n",
           alert->title,
           alert->priority == PRIORITY_HIGH ? "HIGH" : (alert->priority == PRIORITY_MEDIUM ? "MED" : "LOW"),
           alert->summary);
    ui_layout_show_alert(alert);
    sim_audio_play(alert->sound, alert->force_mute);
}

static void on_metrics(const metrics_payload_t* metrics) {
    printf("[Sim Main] Metrics received: ttl=%d, sampled_at=%lld\n",
           metrics->ttl_sec, metrics->sampled_at);
}

static void on_state(work_mode_t mode) {
    const char* mode_names[] = {"IDLE", "CODING", "MEETING", "SLEEP"};
    printf("[Sim Main] State mode updated: %s\n", mode_names[mode % 4]);
    ui_layout_update_status_bar(true, true, mode, "18:30");
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
    while (g_running) {
        sim_mqtt_loop(50);

#ifdef HAS_SDL2
        if (!headless && window) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    g_running = false;
                } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                    printf("[Sim Input] Touch screen tapped!\n");
                    device_action_t act;
                    memset(&act, 0, sizeof(act));
                    strncpy(act.type, "device_action", sizeof(act.type));
                    act.version = 1;
                    strncpy(act.device_id, "esp32_c049ef", sizeof(act.device_id));
                    snprintf(act.action_id, sizeof(act.action_id), "act_%lld", (long long)time(NULL));
                    strncpy(act.action_type, "tap", sizeof(act.action_type));
                    act.timestamp = (long long)time(NULL);
                    sim_mqtt_publish_action(&act);
                }
            }

            // 绘制基础背景 (深色网格舞台)
            SDL_SetRenderDrawColor(renderer, 18, 18, 22, 255);
            SDL_RenderClear(renderer);

            // 1. 顶部状态栏 (高度 24px)
            SDL_SetRenderDrawColor(renderer, 32, 34, 42, 255);
            SDL_Rect status_rect = {0, 0, 240, 24};
            SDL_RenderFillRect(renderer, &status_rect);

            // 状态指示灯: 绿色代表 Wi-Fi/在线
            SDL_SetRenderDrawColor(renderer, 52, 211, 153, 255);
            SDL_Rect dot_rect = {8, 8, 8, 8};
            SDL_RenderFillRect(renderer, &dot_rect);

            const ui_state_t* st = ui_layout_get_state();

            // 2. 精灵舞台区 (56, 32, 128, 128)
            if (st && st->alert_active) {
                // 告警弹出覆盖层
                if (st->active_alert.priority == PRIORITY_HIGH) {
                    SDL_SetRenderDrawColor(renderer, 239, 68, 68, 255); // 红色紧急告警
                } else {
                    SDL_SetRenderDrawColor(renderer, 245, 158, 11, 255); // 琥珀黄中级告警
                }
                SDL_Rect alert_box = {16, 32, 208, 128};
                SDL_RenderDrawRect(renderer, &alert_box);
                SDL_Rect alert_inner = {20, 36, 200, 120};
                SDL_SetRenderDrawColor(renderer, 45, 20, 20, 255);
                SDL_RenderFillRect(renderer, &alert_inner);
            } else {
                // 常态桌宠展示区 (居中 128x128 像素)
                SDL_SetRenderDrawColor(renderer, 40, 44, 56, 255);
                SDL_Rect pet_box = {56, 32, 128, 128};
                SDL_RenderFillRect(renderer, &pet_box);
                SDL_SetRenderDrawColor(renderer, 99, 102, 241, 255); // 蓝紫色呼吸边框
                SDL_RenderDrawRect(renderer, &pet_box);
            }

            // 3. 底部三行度量指标看板 (高度 72px, y: 168~239)
            SDL_SetRenderDrawColor(renderer, 26, 28, 35, 255);
            SDL_Rect metrics_bg = {0, 168, 240, 72};
            SDL_RenderFillRect(renderer, &metrics_bg);

            // Row 1: Kimi (42%)
            SDL_SetRenderDrawColor(renderer, 48, 52, 65, 255);
            SDL_Rect r1_track = {12, 172, 216, 14};
            SDL_RenderFillRect(renderer, &r1_track);
            SDL_SetRenderDrawColor(renderer, 16, 185, 129, 255); // 绿色进度
            SDL_Rect r1_bar = {12, 172, 90, 14};
            SDL_RenderFillRect(renderer, &r1_bar);

            // Row 2: DeepSeek
            SDL_SetRenderDrawColor(renderer, 48, 52, 65, 255);
            SDL_Rect r2_track = {12, 196, 216, 14};
            SDL_RenderFillRect(renderer, &r2_track);
            SDL_SetRenderDrawColor(renderer, 59, 130, 246, 255); // 蓝色进度
            SDL_Rect r2_bar = {12, 196, 140, 14};
            SDL_RenderFillRect(renderer, &r2_bar);

            // Row 3: Copilot
            SDL_SetRenderDrawColor(renderer, 48, 52, 65, 255);
            SDL_Rect r3_track = {12, 220, 216, 14};
            SDL_RenderFillRect(renderer, &r3_track);
            SDL_SetRenderDrawColor(renderer, 168, 85, 247, 255); // 紫色进度
            SDL_Rect r3_bar = {12, 220, 170, 14};
            SDL_RenderFillRect(renderer, &r3_bar);

            SDL_RenderPresent(renderer);
        }
#endif

        usleep(50000); // 50ms tick
        tick_count++;
        if (run_once && tick_count >= 5) {
            // 单次自检退出模式
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

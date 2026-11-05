#include "ui_sakura.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;
static void check_strip(void *ctx, int y, int height, const uint16_t *p) {
    (void)ctx; assert(y == (int)calls * 16); assert(height == 16); assert(p); calls++;
}
int main(void) {
    metrics_payload_t m = {.item_count=1, .sampled_at=1000, .ttl_sec=60};
    m.items[0].total=100; m.items[0].current=32; m.items[0].direction=METRIC_DIR_USED;
    sakura_quota_t q=ui_sakura_quota(&m, 1010);
    assert(q.available && !q.stale && fabsf(q.remaining-.68f)<.001f);
    m.items[0].direction=METRIC_DIR_REMAINING;
    assert(fabsf(ui_sakura_quota(&m,1010).remaining-.32f)<.001f);
    assert(ui_sakura_quota(&m,1061).stale);
    assert(ui_sakura_quota(&m,999).stale);
    m.items[0].is_mock=true; assert(ui_sakura_quota(&m,1010).mock);
    m.items[0].status=METRIC_STATUS_EXHAUSTED;
    q=ui_sakura_quota(&m,1010); assert(q.remaining==0 && !q.stale);
    m.items[0].status=METRIC_STATUS_NORMAL;
    m.items[0].total=0; assert(!ui_sakura_quota(&m,1010).available);
    m.items[0].total=NAN; assert(!ui_sakura_quota(&m,1010).available);
    m.items[0].total=100; m.items[0].current=INFINITY;
    assert(!ui_sakura_quota(&m,1010).available);
    m.items[0].current=-1; assert(!ui_sakura_quota(&m,1010).available);
    m.items[0].current=200; assert(ui_sakura_quota(&m,1010).remaining==1);
    m.items[0].total=0; m.item_count=3;
    m.items[1].total=10; m.items[1].current=5;
    assert(ui_sakura_quota(&m,1010).item_index==1);
    m.item_count=-1; assert(!ui_sakura_quota(&m,1010).available);
    assert(!ui_sakura_quota(NULL,1010).available);
    struct { uint16_t before; uint16_t p[SAKURA_BUFFER_PIXELS]; uint16_t after; } buf;
    buf.before=0x1234; buf.after=0x5678;
    sakura_scene_t scene={.clock="18:30",.wifi=true,.mqtt=true,.now=1010};
    alert_payload_t alert={.priority=PRIORITY_HIGH};
    memset(alert.title,'A',sizeof(alert.title)-1); memset(alert.summary,'B',sizeof(alert.summary)-1);
    scene.alert=&alert;
    for(int mode=0;mode<4;mode++) {
        scene.mode=(work_mode_t)mode; calls=0;
        ui_sakura_render(&scene,buf.p,check_strip,NULL);
        assert(calls==15 && buf.before==0x1234 && buf.after==0x5678);
    }
    puts("Sakura: quota semantics, invalid data, freshness, and strip bounds passed");
}

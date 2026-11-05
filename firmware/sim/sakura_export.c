/* Deterministic offline preview: no network or fabricated live device state. */
#include "ui_sakura.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void ppm_strip(void *ctx,int y,int height,const uint16_t *pixels) {
    (void)y;
    FILE *file=ctx;
    for(int i=0;i<240*height;i++) {
        uint16_t v=pixels[i];
        unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
        fwrite(rgb,1,3,file);
    }
}
int main(int argc,char **argv) {
    if(argc<3) {fprintf(stderr,"Usage: sakura_export idle|focus|meeting|night|done|alert|stale|empty|blink|invalid-utf8 output.ppm\n");return 2;}
    uint16_t strip[SAKURA_BUFFER_PIXELS];
    metrics_payload_t metrics={.item_count=1,.sampled_at=1000,.ttl_sec=60};
    metrics.items[0]=(metric_item_t){.total=100,.current=68,.direction=METRIC_DIR_REMAINING,.is_mock=true};
    alert_payload_t alert={.priority=PRIORITY_MEDIUM,.pet_action=PET_CHEER};
    snprintf(alert.title,sizeof(alert.title),"构建通过");
    snprintf(alert.summary,sizeof(alert.summary),"做好啦，辛苦了！");
    sakura_scene_t scene={.mode=WORK_MODE_IDLE,.clock="18:30",.wifi=true,.mqtt=true,.now=1010,.metrics=&metrics,.alert_remaining_sec=8};
    if(!strcmp(argv[1],"focus"))scene.mode=WORK_MODE_CODING;
    if(!strcmp(argv[1],"meeting")){scene.mode=WORK_MODE_MEETING;scene.muted=true;}
    if(!strcmp(argv[1],"night")){scene.mode=WORK_MODE_SLEEP;scene.clock="23:00";}
    if(!strcmp(argv[1],"done"))scene.alert=&alert;
    if(!strcmp(argv[1],"blink"))scene.elapsed_ms=4250;
    if(!strcmp(argv[1],"stale"))scene.now=1100;
    if(!strcmp(argv[1],"empty")){scene.metrics=NULL;scene.mqtt=false;scene.wifi=false;scene.clock=NULL;}
    if(!strcmp(argv[1],"alert")) {
        scene.alert=&alert;alert.priority=PRIORITY_HIGH;alert.pet_action=PET_ALERT;
        snprintf(alert.title,sizeof(alert.title),"任务失败，需要处理");
        snprintf(alert.summary,sizeof(alert.summary),"请查看客户端详细消息。中文测试 ABC 123");
    }
    if(!strcmp(argv[1],"invalid-utf8")) {
        scene.alert=&alert;alert.priority=PRIORITY_HIGH;
        memset(alert.title,0xe4,sizeof(alert.title));memset(alert.summary,0xf0,sizeof(alert.summary));
    }
    FILE *file=fopen(argv[2],"wb");if(!file){perror(argv[2]);return 1;}
    fprintf(file,"P6\n240 240\n255\n");ui_sakura_render(&scene,strip,ppm_strip,file);
    return fclose(file)?1:0;
}

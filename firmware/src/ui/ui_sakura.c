#include "ui_sakura.h"
#include "assets/sakura_assets.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint16_t *pixels; int top; } canvas_t;
typedef struct { uint16_t bg,ink,muted,accent,soft,line,floor,window,glass,white; } palette_t;
static uint16_t rgb(unsigned v) { return (uint16_t)(((v>>8)&0xf800)|((v>>5)&0x7e0)|((v>>3)&31)); }
static void pixel(canvas_t *c,int x,int y,uint16_t color) {
    if(x>=0 && x<240 && y>=c->top && y<c->top+16) c->pixels[(y-c->top)*240+x]=color;
}
static void rect(canvas_t *c,int x,int y,int w,int h,uint16_t color) {
    int x0=x<0?0:x, x1=x+w>240?240:x+w;
    int y0=y<c->top?c->top:y, y1=y+h>c->top+16?c->top+16:y+h;
    for(int yy=y0;yy<y1;yy++) for(int xx=x0;xx<x1;xx++) pixel(c,xx,yy,color);
}
static void round_rect(canvas_t *c,int x,int y,int w,int h,int r,uint16_t color) {
    int y0=y<c->top?c->top:y, y1=y+h>c->top+16?c->top+16:y+h;
    for(int yy=y0;yy<y1;yy++) {
        int dy=yy<y+r?y+r-1-yy:yy>=y+h-r?yy-(y+h-r):0;
        int inset=0;
        while(inset<r && (r-inset-1)*(r-inset-1)+dy*dy>r*r) inset++;
        rect(c,x+inset,yy,w-inset*2,1,color);
    }
}
static void flower(canvas_t *c,int x,int y,uint16_t color) {
    rect(c,x+3,y,3,9,color);rect(c,x,y+3,9,3,color);rect(c,x+3,y+3,3,3,rgb(0xffe2a0));
}
static void star(canvas_t *c,int x,int y,uint16_t color) {
    rect(c,x+2,y,2,6,color);rect(c,x,y+2,6,2,color);
}
/* Bounded UTF-8 decoder: incomplete/invalid sequences fall back without reading
 * beyond the terminator or a fixed protocol field. */
static uint32_t next_cp(const char **p,const char *end) {
    if(*p>=end || !**p) return 0;
    const unsigned char *s=(const unsigned char *)*p;
    uint32_t cp=*s; int n=1;
    if(*s>=0xc2 && *s<=0xdf) {cp=*s&31;n=2;}
    else if(*s>=0xe0 && *s<=0xef) {cp=*s&15;n=3;}
    else if(*s>=0xf0 && *s<=0xf4) {cp=*s&7;n=4;}
    else if(*s>=128) {(*p)++;return 0x25a1;}
    for(int i=1;i<n;i++) {
        if(*p+i>=end || !s[i] || (s[i]&0xc0)!=0x80) {(*p)++;return 0x25a1;}
        cp=(cp<<6)|(s[i]&63);
    }
    *p+=n;
    if((n==2 && cp<128)||(n==3 && cp<2048)||(n==4 && cp<65536)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff)) return 0x25a1;
    return cp;
}
static const sakura_glyph_t *glyph(uint32_t cp) {
    size_t lo=0,hi=sizeof(sakura_glyphs)/sizeof(sakura_glyphs[0]);
    while(lo<hi) {size_t mid=lo+(hi-lo)/2;if(sakura_glyphs[mid].cp<cp)lo=mid+1;else hi=mid;}
    if(lo<sizeof(sakura_glyphs)/sizeof(sakura_glyphs[0]) && sakura_glyphs[lo].cp==cp) return &sakura_glyphs[lo];
    return glyph(0x25a1);
}
static void draw_glyph(canvas_t *c,int x,int y,uint32_t cp,uint16_t color) {
    const sakura_glyph_t *g=glyph(cp);
    for(int yy=0;yy<12;yy++) for(int xx=0;xx<g->width;xx++)
        if(g->rows[yy]&(1u<<xx)) pixel(c,x+xx,y+yy,color);
}
static int text_width(const char *s) {
    int w=0;const char *end=s+strlen(s);
    while(s<end) w+=glyph(next_cp(&s,end))->width;
    return w;
}
static void text(canvas_t *c,int x,int y,const char *s,uint16_t color) {
    const char *end=s+strlen(s);
    while(s<end) {uint32_t cp=next_cp(&s,end);draw_glyph(c,x,y,cp,color);x+=glyph(cp)->width;}
}
static void center(canvas_t *c,int x,int y,const char *s,uint16_t color) {text(c,x-text_width(s)/2,y,s,color);}
static void right(canvas_t *c,int x,int y,const char *s,uint16_t color) {text(c,x-text_width(s),y,s,color);}
static void message(canvas_t *c,int x,int y,int w,int lines,const char *s,size_t capacity,uint16_t color) {
    const char *end=s;
    for(size_t i=0;i<capacity && *end;i++) end++;
    int xx=x,line=0;
    while(s<end && *s && line<lines) {
        uint32_t cp=next_cp(&s,end);int gw=glyph(cp)->width;
        if(cp=='\n') {line++;xx=x;continue;}
        if(xx+gw>x+w) {line++;xx=x;}
        if(line>=lines) break;
        if(line==lines-1 && xx+gw+12>x+w && s<end && *s) {draw_glyph(c,xx,y+line*16,0x2026,color);break;}
        draw_glyph(c,xx,y+line*16,cp,color);xx+=gw;
    }
}

sakura_quota_t ui_sakura_quota(const metrics_payload_t *m,long long now) {
    sakura_quota_t q={.item_index=-1};
    if(!m) return q;
    for(int i=0;i<m->item_count && i<3;i++) {
        const metric_item_t *v=&m->items[i];
        if(!isfinite(v->total)||!isfinite(v->current)||v->total<=0||v->current<0) continue;
        if(v->direction!=METRIC_DIR_USED && v->direction!=METRIC_DIR_REMAINING) continue;
        double ratio=v->current/v->total;
        if(ratio>1) ratio=1;
        q.available=true;q.item_index=i;
        q.remaining=(float)(v->direction==METRIC_DIR_USED?1-ratio:ratio);
        if(v->status==METRIC_STATUS_EXHAUSTED) q.remaining=0;
        long long sampled=v->sampled_at>0?v->sampled_at:m->sampled_at;
        int ttl=v->ttl_sec>0?v->ttl_sec:m->ttl_sec;
        q.stale=sampled<=0 || now<sampled || (ttl>0 && now-sampled>ttl);
        q.mock=v->is_mock;return q;
    }
    return q;
}
static void character(canvas_t *c,uint32_t ms,bool night) {
    int cycle=ms%5300; bool closed=night || (cycle>=4150 && cycle<4375);
    int breathe=!night && ms%2400>=600 && ms%2400<1800;
    const uint16_t *colors=closed?sakura_blink:sakura_open;
    const uint8_t *mask=closed?sakura_blink_mask:sakura_open_mask;
    int h=140+breathe,y0=178-h;
    for(int y=c->top;y<c->top+16;y++) {
        if(y<y0 || y>=178) continue;
        int sy=(y-y0)*120/h;
        for(int x=0;x<78;x++) {int i=sy*67+x*67/78;if(mask[i]) pixel(c,81+x,y,colors[i]);}
    }
}
static void scene_strip(canvas_t *c,const sakura_scene_t *s) {
    bool night=s->mode==WORK_MODE_SLEEP;
    bool focus=s->mode==WORK_MODE_CODING;
    bool cheer=s->alert && s->alert->pet_action==PET_CHEER;
    palette_t t;
    if(night) t=(palette_t){rgb(0x29293f),rgb(0xfff0f8),rgb(0xc7b9d6),rgb(0xff92c3),rgb(0x3b334d),rgb(0x564263),rgb(0x333044),rgb(0x6c527f),rgb(0x37344f),rgb(0x43364f)};
    else t=(palette_t){rgb(0xfff8fc),rgb(0x63384f),rgb(0x986079),rgb(0xef4287),rgb(0xffe4f0),rgb(0xf2cfdf),rgb(0xffedf5),rgb(0xffc6df),rgb(0xfff4fa),rgb(0xffffff)};
    rect(c,0,0,240,240,t.bg);
    flower(c,12,11,t.accent);text(c,28,10,"SAKURA",t.ink);
    right(c,226,10,s->clock?s->clock:"--:--",t.ink);
    rect(c,13,29,3,3,s->mqtt?t.accent:rgb(0xc59aaa));
    const char *status=night?"休息时间":focus?"陪你专注":s->mode==WORK_MODE_MEETING?"安静陪你开会":cheer?"有个好消息":"在线 · 陪着你";
    if(!s->mqtt) status=s->wifi?"连接中":"离线 · 陪着你";
    if(s->muted) status="静音 · 陪着你";
    text(c,22,25,status,t.muted);
    rect(c,0,167,240,18,t.floor);rect(c,12,166,216,1,t.line);
    round_rect(c,60,43,120,123,52,t.window);round_rect(c,65,48,110,118,48,t.glass);
    rect(c,118,49,4,114,t.window);rect(c,66,98,108,4,t.window);
    if(night) {round_rect(c,145,64,12,12,6,rgb(0xffe3ae));round_rect(c,149,61,12,12,6,t.glass);}
    else {round_rect(c,77,72,28,5,2,t.white);round_rect(c,83,68,14,5,2,t.white);round_rect(c,136,116,26,4,2,t.white);}
    rect(c,57,163,126,4,t.window);
    rect(c,28,140,18,4,t.accent);rect(c,31,144,12,16,night?rgb(0xad759a):rgb(0xffb1d2));
    rect(c,36,123,2,17,night?rgb(0x9f98b7):rgb(0x82b09b));flower(c,32,116,night?rgb(0xd9a3c1):rgb(0xf774b0));
    rect(c,186,145,31,4,t.window);rect(c,190,149,3,18,t.line);rect(c,211,149,3,18,t.line);
    rect(c,193,136,9,8,t.accent);rect(c,202,138,3,4,t.accent);rect(c,192,134,11,2,t.white);
    star(c,39,66,t.window);star(c,195,96,t.window);
    for(int y=174;y<180;y++)for(int x=91;x<149;x++)if((x-120)*(x-120)*9+(y-177)*(y-177)*841<=7569)pixel(c,x,y,t.line);
    character(c,s->elapsed_ms,night);
    if(focus) {round_rect(c,133,144,32,23,2,t.window);rect(c,137,148,24,15,t.soft);rect(c,130,167,38,3,t.accent);star(c,147,152,t.accent);}
    if(cheer) {star(c,66,79,t.accent);star(c,173,103,t.accent);star(c,184,68,t.window);}
    if(night) {text(c,171,68,"z",t.muted);text(c,183,54,"z",t.muted);}
    round_rect(c,12,187,216,25,7,t.white);rect(c,113,184,7,4,t.white);
    const char *speech=night?"明天也一起加油。":focus?"你写代码，我陪你。":s->mode==WORK_MODE_MEETING?"安静陪你开会。":"我在，慢慢来。";
    if(cheer) speech="做好啦，辛苦了！";
    center(c,120,194,speech,t.ink);
    sakura_quota_t quota=ui_sakura_quota(s->metrics,s->now);
    text(c,13,222,"剩余额度",t.ink);
    if(quota.available) {
        char value[64];
        if(s->show_value && !quota.stale) {
            const metric_item_t *v=&s->metrics->items[quota.item_index];
            snprintf(value,sizeof(value),"%.0f/%.0f %.15s",v->total*quota.remaining,v->total,v->unit);
            message(c,82,222,144,1,value,sizeof(value),t.accent);
        } else {
            round_rect(c,82,225,97,5,2,t.soft);
            int fill=(int)(97*quota.remaining+.5f);
            if(fill) round_rect(c,82,225,fill,5,fill<4?0:2,quota.stale?t.muted:t.accent);
            snprintf(value,sizeof(value),"%d%%",(int)(quota.remaining*100+.5f));
            right(c,226,222,quota.stale?"过期":value,quota.stale?t.muted:t.accent);
        }
        if(quota.mock) right(c,226,25,"演示",t.muted);
    } else right(c,226,222,"--",t.muted);
    if(cheer && s->alert->priority!=PRIORITY_HIGH) {
        /* Completed tasks keep the room visible, with the real event text. */
        round_rect(c,12,187,216,25,7,t.white);
        message(c,22,194,196,1,s->alert->summary[0]?s->alert->summary:"做好啦，辛苦了！",sizeof(s->alert->summary),t.ink);
        rect(c,0,219,240,21,t.bg);
        round_rect(c,13,222,13,13,3,t.accent);star(c,16,225,t.white);
        message(c,34,222,149,1,s->alert->title,sizeof(s->alert->title),t.ink);
        char countdown[16];snprintf(countdown,sizeof(countdown),"%ds",s->alert_remaining_sec>0?s->alert_remaining_sec:0);
        right(c,226,222,countdown,t.muted);
    } else if(s->alert) {
        const alert_payload_t *a=s->alert;
        uint16_t bg=a->priority==PRIORITY_HIGH?rgb(0xffe4e9):t.white;
        round_rect(c,10,43,220,138,10,t.accent);round_rect(c,12,45,216,134,8,bg);
        text(c,22,51,a->priority==PRIORITY_HIGH?"需要处理":"收到新通知",t.accent);
        message(c,22,72,196,2,a->title,sizeof(a->title),t.ink);
        message(c,22,109,196,2,a->summary,sizeof(a->summary),t.ink);
        if(a->priority==PRIORITY_HIGH) text(c,22,151,"请在客户端确认",t.accent);
        else {char countdown[32];snprintf(countdown,sizeof(countdown),"%ds",s->alert_remaining_sec>0?s->alert_remaining_sec:0);text(c,22,151,countdown,t.muted);}
        round_rect(c,12,187,216,25,7,t.white);
        if(s->touch_enabled) center(c,120,194,"按下打开 · 长按关闭",t.ink);
        else center(c,120,194,"详细消息请查看客户端",t.ink);
    }
}
void ui_sakura_render(const sakura_scene_t *scene,uint16_t *strip,sakura_flush_t flush,void *ctx) {
    if(!scene||!strip||!flush)return;
    canvas_t c={.pixels=strip};
    for(c.top=0;c.top<240;c.top+=16) {scene_strip(&c,scene);flush(ctx,c.top,16,strip);}
}

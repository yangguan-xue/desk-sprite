#ifndef MQTT_REASSEMBLER_H
#define MQTT_REASSEMBLER_H

#include <stdbool.h>
#include <stddef.h>

#define MQTT_MAX_REASSEMBLE_PAYLOAD_SIZE 2048
#define MQTT_MAX_TOPIC_SIZE 128

typedef enum {
    MQTT_REASSEMBLE_NEED_MORE = 0,      // 分片已被缓存，等待后续分片
    MQTT_REASSEMBLE_COMPLETE  = 1,      // 全部片段已按序到达，报文完整可解析
    MQTT_REASSEMBLE_ERR_OVERSIZED = -1, // 总长度超过 2048 字节有界上限
    MQTT_REASSEMBLE_ERR_OFFSET    = -2, // 分片偏移不连续或非预期分片
    MQTT_REASSEMBLE_ERR_INVALID   = -3  // 参数非法 (如空指针或长度负数)
} mqtt_reassemble_result_t;

typedef struct {
    char topic[MQTT_MAX_TOPIC_SIZE];
    char buffer[MQTT_MAX_REASSEMBLE_PAYLOAD_SIZE + 1];
    int total_len;
    int received_len;
    bool in_progress;
} mqtt_reassembler_t;

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_reassembler_init(mqtt_reassembler_t *reassembler);
void mqtt_reassembler_reset(mqtt_reassembler_t *reassembler);

mqtt_reassemble_result_t mqtt_reassembler_feed(
    mqtt_reassembler_t *reassembler,
    const char *topic,
    int topic_len,
    const char *data,
    int data_len,
    int total_data_len,
    int current_data_offset,
    const char **out_topic,
    const char **out_payload,
    int *out_payload_len
);

#ifdef __cplusplus
}
#endif

#endif // MQTT_REASSEMBLER_H

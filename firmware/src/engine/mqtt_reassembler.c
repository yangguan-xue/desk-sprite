#include "mqtt_reassembler.h"
#include <string.h>

void mqtt_reassembler_init(mqtt_reassembler_t *reassembler) {
    if (!reassembler) return;
    memset(reassembler, 0, sizeof(mqtt_reassembler_t));
}

void mqtt_reassembler_reset(mqtt_reassembler_t *reassembler) {
    if (!reassembler) return;
    reassembler->topic[0] = '\0';
    reassembler->buffer[0] = '\0';
    reassembler->total_len = 0;
    reassembler->received_len = 0;
    reassembler->in_progress = false;
}

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
) {
    if (!reassembler || !data || data_len < 0 || total_data_len <= 0 || current_data_offset < 0) {
        return MQTT_REASSEMBLE_ERR_INVALID;
    }

    // 严格限制最大总长度 2048 字节 (SPEC-002 第 1 节, R5-02)
    if (total_data_len > MQTT_MAX_REASSEMBLE_PAYLOAD_SIZE) {
        mqtt_reassembler_reset(reassembler);
        return MQTT_REASSEMBLE_ERR_OVERSIZED;
    }

    if (current_data_offset == 0) {
        // 首片报文: 必须携带有效 topic
        if (!topic || topic_len <= 0) {
            mqtt_reassembler_reset(reassembler);
            return MQTT_REASSEMBLE_ERR_INVALID;
        }

        mqtt_reassembler_reset(reassembler);

        int tlen = topic_len < (MQTT_MAX_TOPIC_SIZE - 1) ? topic_len : (MQTT_MAX_TOPIC_SIZE - 1);
        memcpy(reassembler->topic, topic, tlen);
        reassembler->topic[tlen] = '\0';

        if (data_len > total_data_len) {
            data_len = total_data_len;
        }

        memcpy(reassembler->buffer, data, data_len);
        reassembler->total_len = total_data_len;
        reassembler->received_len = data_len;
        reassembler->in_progress = (reassembler->received_len < reassembler->total_len);
    } else {
        // 后续分片: 必须在前序分片正在进行中
        if (!reassembler->in_progress) {
            mqtt_reassembler_reset(reassembler);
            return MQTT_REASSEMBLE_ERR_OFFSET;
        }

        // 总长度必须与首片声明一致
        if (total_data_len != reassembler->total_len) {
            mqtt_reassembler_reset(reassembler);
            return MQTT_REASSEMBLE_ERR_OFFSET;
        }

        // 偏移必须严格连续 (不连续或乱序片段拒绝)
        if (current_data_offset != reassembler->received_len) {
            mqtt_reassembler_reset(reassembler);
            return MQTT_REASSEMBLE_ERR_OFFSET;
        }

        // 校验长度防止溢出
        if (current_data_offset + data_len > total_data_len ||
            current_data_offset + data_len > MQTT_MAX_REASSEMBLE_PAYLOAD_SIZE) {
            mqtt_reassembler_reset(reassembler);
            return MQTT_REASSEMBLE_ERR_OVERSIZED;
        }

        memcpy(reassembler->buffer + current_data_offset, data, data_len);
        reassembler->received_len += data_len;
    }

    // 检查是否已收齐全部数据
    if (reassembler->received_len >= reassembler->total_len) {
        reassembler->buffer[reassembler->total_len] = '\0';
        if (out_topic) *out_topic = reassembler->topic;
        if (out_payload) *out_payload = reassembler->buffer;
        if (out_payload_len) *out_payload_len = reassembler->total_len;
        reassembler->in_progress = false;
        return MQTT_REASSEMBLE_COMPLETE;
    }

    return MQTT_REASSEMBLE_NEED_MORE;
}

"""Verify deployed HTTPS and MQTT TLS using real credentials, without printing them."""

import argparse
import json
from pathlib import Path
import queue
import ssl
import threading
import time
import urllib.error
import urllib.request
import uuid

import paho.mqtt.client as mqtt

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--credentials", type=Path, required=True)
parser.add_argument("--api-url", default="https://voice-code.yangguanxue.top/sprite")
parser.add_argument("--mqtt-host", default="voice-code.yangguanxue.top")
parser.add_argument("--mqtt-port", type=int, default=8883)
args = parser.parse_args()
credentials = json.loads(args.credentials.read_text())
tls = ssl.create_default_context()


def request(path, body=None, key=None, idempotency=None):
    headers = {}
    if key:
        headers["Authorization"] = "Bearer " + key
    if idempotency:
        headers["Idempotency-Key"] = idempotency
    data = None if body is None else json.dumps(body).encode()
    if data:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(args.api_url + path, data=data, headers=headers)
    try:
        response = urllib.request.urlopen(req, context=tls, timeout=10)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, json.load(response)


status, health = request("/health")
assert status == 200 and health["data"]["mqtt_connected"], health
print("PASS HTTPS health and real cloud-to-Broker connection")
body = {"target_device": "esp32_c049ef", "title": "云端部署验收", "summary": "真实 MQTT TLS 通知测试"}
assert request("/api/v1/notify", body, "invalid-deployment-probe-key")[0] == 401
assert request("/api/v1/notify", body, "spr_live_test_key")[0] == 401
print("PASS invalid and repository test API tokens rejected")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, protocol=mqtt.MQTTv5)
client.username_pw_set("dev_esp32_c049ef", credentials["dev_esp32_c049ef"])
client.tls_set_context(tls)
subscribed = threading.Event()
messages = queue.Queue()
publish_results = queue.Queue()
topic = "sprite/device/esp32_c049ef/down/alert"


def receive_event(event_id, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            payload = messages.get(timeout=max(0, deadline - time.monotonic()))
        except queue.Empty:
            return None
        if payload.get("event_id") == event_id:
            return payload
    return None


def on_connect(client, userdata, flags, reason, properties):
    if not reason.is_failure:
        client.subscribe(topic, qos=1)


def on_subscribe(client, userdata, mid, reasons, properties):
    if reasons and all(not code.is_failure for code in reasons):
        subscribed.set()


client.on_connect = on_connect
client.on_subscribe = on_subscribe
client.on_message = lambda client, userdata, message: messages.put(json.loads(message.payload))
client.on_publish = lambda client, userdata, mid, reason, properties: publish_results.put((mid, reason.value))
try:
    client.connect(args.mqtt_host, args.mqtt_port, keepalive=30)
    client.loop_start()
    assert subscribed.wait(10), "Device TLS subscription did not succeed"
    idempotency = "deployment-" + uuid.uuid4().hex
    status, sent = request("/api/v1/notify", body, credentials["api_key"], idempotency)
    assert status == 200, sent
    event_id = sent["data"]["event_id"]
    received = receive_event(event_id, timeout=10)
    assert received is not None, "Matching notification was not received"
    print("PASS HTTPS notification received by device identity over MQTT TLS:", event_id)
    status, repeated = request("/api/v1/notify", body, credentials["api_key"], idempotency)
    assert status == 200 and repeated["data"]["event_id"] == event_id
    assert request("/api/v1/notify", {**body, "summary": "changed body"},
                   credentials["api_key"], idempotency)[0] == 409
    assert receive_event(event_id, timeout=1) is None, "Duplicate notification was published"
    print("PASS idempotent retry, no duplicate publish, conflicting body rejected")
    published = client.publish("sprite/device/another_device/up/action", "{}", qos=1)
    mid, reason = publish_results.get(timeout=5)
    assert mid == published.mid and reason == 135, (mid, reason)
    print("PASS cross-device MQTT publish rejected by Broker ACL (Not authorized)")
finally:
    client.disconnect()
    client.loop_stop()

rejected = threading.Event()
bad = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, protocol=mqtt.MQTTv5)
bad.username_pw_set("dev_esp32_c049ef", "invalid-deployment-probe-password")
bad.tls_set_context(tls)
bad.on_connect = lambda client, userdata, flags, reason, properties: rejected.set() if reason.is_failure else None
try:
    bad.connect(args.mqtt_host, args.mqtt_port, keepalive=30)
    bad.loop_start()
    assert rejected.wait(5), "Invalid MQTT password was not rejected"
    print("PASS invalid MQTT password rejected")
finally:
    bad.disconnect()
    bad.loop_stop()

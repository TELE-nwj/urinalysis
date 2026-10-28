#include "mobile.h"
#include "driver/mobile_link/mobile_link.h"
#include "driver/wifi/wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_random.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define FRAME_MAX 1024
#define CACHE_SIZE 32
#define FIELD_MAX 9

typedef struct { char key[65], value[65]; char kind; } field_t;
typedef struct { field_t fields[FIELD_MAX]; unsigned count; } object_t;
typedef struct {
    bool used, terminal, submitting;
    uint64_t token, order;
    uint32_t req_id;
    bool valid;
    service_mobile_command_t command;
    uint8_t hour, minute, second;
    const char *phase;
    service_mobile_result_t result;
    service_mobile_state_t state;
    uint64_t state_seq;
} record_t;
static struct {
    SemaphoreHandle_t mutex, ack_mutex;
    bool running, dirty, hello_pending, ap_owned, link_owned;
    service_mobile_config_t cfg;
    char device_id[65], boot_id[65], session_id[65];
    mobile_link_peer_t peer;
    uint64_t next_token, order, last_send_ms, last_state_seq;
    record_t records[CACHE_SIZE];
} svc;

static const char *const states[] = { "IDLE", "MEASURING", "PAUSED" };
static const char *const storage[] = { "UNKNOWN", "ABSENT", "READY", "ERROR" };
static const char *const results[] = { "OK", "BUSY", "EXPIRED", "INVALID_ARGUMENT", "NOT_READY", "INTERNAL_ERROR" };
static void lock(void) { xSemaphoreTake(svc.mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(svc.mutex); }
static bool id_valid(const char *s)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (!n || n > 64) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
static void make_id(char *out, const char *prefix)
{
    snprintf(out, 65, "%s-%08x%08x%08x%08x", prefix,
             (unsigned)esp_random(), (unsigned)esp_random(),
             (unsigned)esp_random(), (unsigned)esp_random());
}
static void whitespace(const char **p, const char *end)
{ while (*p < end && (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')) (*p)++; }
static int hex(char c)
{ return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* All protocol strings are ASCII. Decode JSON escapes before duplicate checks. */
static bool string(const char **p, const char *end, char *out)
{
    if (*p == end || *(*p)++ != '"') return false;
    unsigned n = 0;
    while (*p < end) {
        unsigned char c = (unsigned char)*(*p)++;
        if (c == '"') { out[n] = 0; return true; }
        if (c == '\\') {
            if (*p == end) return false;
            c = (unsigned char)*(*p)++;
            if (c == 'u') {
                unsigned v = 0;
                for (unsigned i = 0; i < 4; i++) {
                    if (*p == end) return false;
                    int d = hex(*(*p)++); if (d < 0) return false;
                    v = v * 16 + (unsigned)d;
                }
                if (!v || v > 127) return false;
                c = (unsigned char)v;
            } else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c != '"' && c != '\\' && c != '/') return false;
        } else if (c < 32 || c >= 128) return false;
        if (n == 64) return false;
        out[n++] = (char)c;
    }
    return false;
}
static bool number(const char **p, const char *end, char *out)
{
    const char *start = *p;
    if (*p < end && **p == '-') (*p)++;
    if (*p == end) return false;
    if (**p == '0') (*p)++;
    else { if (**p < '1' || **p > '9') return false; do { (*p)++; } while (*p < end && **p >= '0' && **p <= '9'); }
    if (*p < end && **p == '.') {
        (*p)++; const char *digits = *p;
        while (*p < end && **p >= '0' && **p <= '9') (*p)++;
        if (*p == digits) return false;
    }
    if (*p < end && (**p == 'e' || **p == 'E')) {
        (*p)++; if (*p < end && (**p == '+' || **p == '-')) (*p)++;
        const char *digits = *p;
        while (*p < end && **p >= '0' && **p <= '9') (*p)++;
        if (*p == digits) return false;
    }
    size_t n = (size_t)(*p - start); if (n > 64) return false;
    memcpy(out, start, n); out[n] = 0; return true;
}
static bool parse(const char *text, size_t len, object_t *out)
{
    const char *p = text, *end = text + len;
    out->count = 0; whitespace(&p, end);
    if (p == end || *p++ != '{') return false;
    whitespace(&p, end);
    if (p < end && *p == '}') { p++; whitespace(&p, end); return p == end; }
    for (;;) {
        if (out->count == FIELD_MAX) return false;
        field_t *f = &out->fields[out->count];
        if (!string(&p, end, f->key)) return false;
        for (unsigned i = 0; i < out->count; i++) if (!strcmp(f->key, out->fields[i].key)) return false;
        whitespace(&p, end); if (p == end || *p++ != ':') return false;
        whitespace(&p, end); if (p == end) return false;
        if (*p == '"') { f->kind = 's'; if (!string(&p, end, f->value)) return false; }
        else if (*p == '-' || (*p >= '0' && *p <= '9')) { f->kind = 'n'; if (!number(&p, end, f->value)) return false; }
        else {
            f->kind = 'l'; unsigned n = 0;
            while (p < end && *p >= 'a' && *p <= 'z' && n < 64) f->value[n++] = *p++;
            f->value[n] = 0;
            if (strcmp(f->value, "true") && strcmp(f->value, "false") && strcmp(f->value, "null")) return false;
        }
        out->count++; whitespace(&p, end);
        if (p == end) return false;
        if (*p == '}') { p++; whitespace(&p, end); return p == end; }
        if (*p++ != ',') return false;
        whitespace(&p, end);
    }
}
static const field_t *find(const object_t *o, const char *key)
{ for (unsigned i = 0; i < o->count; i++) if (!strcmp(o->fields[i].key, key)) return &o->fields[i]; return NULL; }
static bool integer(const field_t *f, uint32_t max, uint32_t *out)
{
    if (!f || f->kind != 'n' || !f->value[0]) return false;
    uint32_t v = 0;
    for (const char *p = f->value; *p; p++) {
        if (*p < '0' || *p > '9' || v > max / 10 ||
            (v == max / 10 && (uint32_t)(*p - '0') > max % 10)) return false;
        v = v * 10 + (uint32_t)(*p - '0');
    }
    *out = v; return true;
}
static bool equals(const field_t *f, const char *s) { return f && f->kind == 's' && !strcmp(f->value, s); }
static bool validate(const object_t *o, const char *session, service_mobile_request_t *r)
{
    uint32_t n, h, m, s;
    if (!integer(find(o, "v"), 1, &n) || n != 1 || !equals(find(o, "session_id"), session)) return false;
    const field_t *sid = find(o, "session_id"); if (!id_valid(sid->value)) return false;
    if (equals(find(o, "type"), "command")) {
        if (o->count != 5) return false;
        if (equals(find(o, "action"), "start")) r->command = SERVICE_MOBILE_START;
        else if (equals(find(o, "action"), "pause")) r->command = SERVICE_MOBILE_PAUSE;
        else if (equals(find(o, "action"), "reset")) r->command = SERVICE_MOBILE_RESET;
        else return false;
    } else if (equals(find(o, "type"), "time_sync")) {
        if (o->count != 7 || !integer(find(o, "hour"), 23, &h) ||
            !integer(find(o, "minute"), 59, &m) || !integer(find(o, "second"), 59, &s)) return false;
        r->command = SERVICE_MOBILE_TIME_SYNC; r->hour = (uint8_t)h; r->minute = (uint8_t)m; r->second = (uint8_t)s;
    } else return false;
    return true;
}
/* Called only while holding the independent send gate. */
static bool hello_locked(mobile_link_peer_t peer, const char *session)
{
    lock();
    if (!svc.running || svc.peer != peer || strcmp(session, svc.session_id)) { unlock(); return false; }
    bool pending = svc.hello_pending; unlock();
    if (!pending) return true;
    char msg[320];
    int n = snprintf(msg, sizeof(msg), "{\"v\":1,\"type\":\"hello\",\"device_id\":\"%s\",\"boot_id\":\"%s\",\"session_id\":\"%s\"}", svc.device_id, svc.boot_id, session);
    if (n < 0 || (size_t)n >= sizeof(msg) || mobile_link_send_text(peer, msg, (size_t)n) != ERR_OK) return false;
    lock(); bool current = svc.running && svc.peer == peer && !strcmp(session, svc.session_id);
    if (current) svc.hello_pending = false;
    unlock(); return current;
}
static void send_error(mobile_link_peer_t peer)
{
    const char msg[] = "{\"v\":1,\"type\":\"error\",\"result\":\"INVALID_ARGUMENT\"}";
    char session[65];
    xSemaphoreTake(svc.ack_mutex, portMAX_DELAY);
    lock(); memcpy(session, svc.session_id, sizeof(session)); unlock();
    if (hello_locked(peer, session)) mobile_link_send_text(peer, msg, sizeof(msg) - 1);
    xSemaphoreGive(svc.ack_mutex);
}
static void send_ack(mobile_link_peer_t peer, const char *session, const record_t *cached, bool refresh)
{
    char msg[384]; record_t latest = *cached;
    /* This gate serializes ACK queue insertion, independently of the data
     * mutex. Refresh after acquiring it so a copied accepted reply cannot
     * follow a completion that acquired the gate first. */
    xSemaphoreTake(svc.ack_mutex, portMAX_DELAY);
    if (!hello_locked(peer, session)) { xSemaphoreGive(svc.ack_mutex); return; }
    lock();
    if (!svc.running || svc.peer != peer || strcmp(session, svc.session_id)) {
        unlock(); xSemaphoreGive(svc.ack_mutex); return;
    }
    if (refresh) {
        bool found = false;
        for (unsigned i = 0; i < CACHE_SIZE; i++) {
            record_t *slot = &svc.records[i];
            if (slot->used && slot->req_id == cached->req_id && slot->token == cached->token) {
                latest = *slot; found = true; break;
            }
        }
        if (!found || latest.submitting) { unlock(); xSemaphoreGive(svc.ack_mutex); return; }
    }
    unlock(); const record_t *r = &latest;
    int n = snprintf(msg, sizeof(msg), "{\"v\":1,\"type\":\"ack\",\"boot_id\":\"%s\",\"session_id\":\"%s\",\"req_id\":%u,\"phase\":\"%s\",\"result\":\"%s\",\"state\":\"%s\",\"state_seq\":%llu}", svc.boot_id, session, (unsigned)r->req_id, r->phase, results[r->result], states[r->state], (unsigned long long)r->state_seq);
    if (n > 0 && (size_t)n < sizeof(msg)) mobile_link_send_text(peer, msg, (size_t)n);
    xSemaphoreGive(svc.ack_mutex);
}
static void on_open(mobile_link_peer_t peer, void *ctx)
{
    (void)ctx; char session[65];
    lock(); if (!svc.running || svc.peer || !peer) { unlock(); return; }
    svc.peer = peer; make_id(svc.session_id, "session"); memcpy(session, svc.session_id, sizeof(session));
    memset(svc.records, 0, sizeof(svc.records)); svc.dirty = true; svc.hello_pending = true; svc.last_send_ms = 0; svc.last_state_seq = 0;
    unlock();
    xSemaphoreTake(svc.ack_mutex, portMAX_DELAY);
    hello_locked(peer, session);
    xSemaphoreGive(svc.ack_mutex);
}
static void on_close(mobile_link_peer_t peer, void *ctx)
{
    (void)ctx; lock(); if (svc.peer == peer) { svc.peer = 0; memset(svc.records, 0, sizeof(svc.records)); } unlock();
}
static service_mobile_result_t map_error(err_t e)
{ return e == ERR_BUSY ? SERVICE_MOBILE_BUSY : e == ERR_NOT_READY ? SERVICE_MOBILE_NOT_READY : e == ERR_PARAM ? SERVICE_MOBILE_INVALID_ARGUMENT : SERVICE_MOBILE_INTERNAL_ERROR; }
static void on_text(mobile_link_peer_t peer, const char *text, size_t len, void *ctx)
{
    (void)ctx; char session[65];
    lock(); if (!svc.running || svc.peer != peer) { unlock(); return; }
    memcpy(session, svc.session_id, sizeof(session)); unlock();
    object_t o; uint32_t req_id;
    if (!text || !len || len > FRAME_MAX || !parse(text, len, &o) ||
        !integer(find(&o, "req_id"), 2147483647u, &req_id) || !req_id) { send_error(peer); return; }
    service_mobile_request_t request = { 0 };
    bool valid = validate(&o, session, &request);
    service_mobile_snapshot_t snap = { 0 }; bool known = svc.cfg.snapshot(&snap, svc.cfg.ctx);
    record_t reply = { .req_id = req_id, .phase = "rejected", .result = SERVICE_MOBILE_INVALID_ARGUMENT };
    if (known && (unsigned)snap.state <= SERVICE_MOBILE_PAUSED) { reply.state = snap.state; reply.state_seq = snap.state_seq; }
    /* A delayed old-session frame must not reserve a req_id in this session. */
    if (!equals(find(&o, "session_id"), session)) { send_ack(peer, session, &reply, false); return; }
    uint64_t now = svc.cfg.monotonic_ms(svc.cfg.ctx);
    lock(); if (svc.peer != peer || strcmp(session, svc.session_id)) { unlock(); return; }
    record_t *slot = NULL;
    for (unsigned i = 0; i < CACHE_SIZE; i++) {
        record_t *r = &svc.records[i];
        if (r->used && r->req_id == req_id) {
            /* The strict schema leaves only command and HMS as content.
             * Invalid shapes always yield INVALID_ARGUMENT, and reserving
             * their IDs also prevents a corrected request executing later. */
            bool same = valid && r->valid && r->command == request.command &&
                r->hour == request.hour && r->minute == request.minute && r->second == request.second;
            bool refresh = same || (!valid && !r->valid);
            if (refresh) reply = *r;
            unlock(); if (!reply.submitting) send_ack(peer, session, &reply, refresh); return;
        }
        if (!r->used) slot = r;
    }
    if (!slot) for (unsigned i = 0; i < CACHE_SIZE; i++) {
        record_t *r = &svc.records[i];
        if (r->terminal && (!slot || r->order < slot->order)) slot = r;
    }
    if (!slot) { reply.result = SERVICE_MOBILE_BUSY; unlock(); send_ack(peer, session, &reply, false); return; }
    *slot = reply; slot->used = true; slot->order = ++svc.order;
    slot->valid = valid; slot->command = request.command;
    slot->hour = request.hour; slot->minute = request.minute; slot->second = request.second;
    slot->terminal = !valid;
    if (!valid) { reply = *slot; unlock(); send_ack(peer, session, &reply, true); return; }
    slot->submitting = true; slot->token = ++svc.next_token; request.token = slot->token;
    request.expires_uptime_ms = now > UINT64_MAX - 5000 ? UINT64_MAX : now + 5000;
    unlock();
    err_t e = svc.cfg.submit(&request, svc.cfg.ctx);
    lock();
    if (svc.peer != peer || strcmp(session, svc.session_id) || slot->token != request.token) { unlock(); return; }
    slot->submitting = false;
    if (!slot->terminal) {
        slot->phase = e == ERR_OK ? "accepted" : "rejected";
        slot->result = e == ERR_OK ? SERVICE_MOBILE_OK : map_error(e);
        slot->terminal = e != ERR_OK;
    }
    reply = *slot; unlock(); send_ack(peer, session, &reply, true);
}

err_t service_mobile_start(const service_mobile_config_t *config)
{
    if (!config || !id_valid(config->device_id) || !config->submit || !config->snapshot || !config->monotonic_ms) return ERR_PARAM;
    if (!svc.mutex) { svc.mutex = xSemaphoreCreateMutex(); if (!svc.mutex) return ERR_NO_MEM; }
    if (!svc.ack_mutex) { svc.ack_mutex = xSemaphoreCreateMutex(); if (!svc.ack_mutex) return ERR_NO_MEM; }
    lock(); if (svc.running || svc.ap_owned || svc.link_owned) { unlock(); return ERR_BUSY; }
    svc.cfg = *config; memcpy(svc.device_id, config->device_id, strlen(config->device_id) + 1);
    if (!svc.boot_id[0]) make_id(svc.boot_id, "boot");
    unlock();
    wifi_ap_config_t ap = { config->ap_ssid, config->ap_password, config->ap_channel, config->ap_max_connections };
    err_t e = wifi_ap_start(&ap); if (e != ERR_OK) return e;
    lock(); svc.ap_owned = true; unlock();
    mobile_link_config_t link = { 80, "/ws", FRAME_MAX };
    mobile_link_callbacks_t callbacks = { on_open, on_text, on_close, NULL };
    lock(); svc.running = true; unlock();
    e = mobile_link_start(&link, &callbacks);
    if (e != ERR_OK) {
        lock(); svc.running = false; unlock(); wifi_ap_stop();
        bool running = wifi_ap_is_running();
        lock(); svc.ap_owned = running; unlock();
    } else { lock(); svc.link_owned = true; unlock(); }
    return e;
}
void service_mobile_stop(void)
{
    if (!svc.mutex) return;
    lock(); svc.running = false; svc.peer = 0; memset(svc.records, 0, sizeof(svc.records));
    bool link_owned = svc.link_owned, ap_owned = svc.ap_owned; unlock();
    if (link_owned) {
        mobile_link_stop(); /* Driver returns only after its owned task drains. */
        lock(); svc.link_owned = false; unlock();
    }
    if (ap_owned) {
        wifi_ap_stop(); bool running = wifi_ap_is_running();
        lock(); svc.ap_owned = running; unlock();
    }
}
void service_mobile_complete(uint64_t token, service_mobile_result_t result,
                             service_mobile_state_t state, uint64_t state_seq)
{
    if (!svc.mutex || !token || (unsigned)result > SERVICE_MOBILE_INTERNAL_ERROR || (unsigned)state > SERVICE_MOBILE_PAUSED) return;
    record_t reply; char session[65]; mobile_link_peer_t peer = 0;
    lock();
    if (svc.running && svc.peer) for (unsigned i = 0; i < CACHE_SIZE; i++) {
        record_t *r = &svc.records[i];
        if (r->used && r->token == token && !r->terminal) {
            r->terminal = true; r->phase = "completed"; r->result = result; r->state = state; r->state_seq = state_seq;
            svc.dirty = true; reply = *r;
            if (!r->submitting) peer = svc.peer;
            memcpy(session, svc.session_id, sizeof(session)); break;
        }
    }
    unlock(); if (peer) send_ack(peer, session, &reply, true);
}
static bool snapshot_valid(const service_mobile_snapshot_t *s)
{
    if ((unsigned)s->state > SERVICE_MOBILE_PAUSED || (unsigned)s->storage_state > SERVICE_MOBILE_STORAGE_ERROR ||
        s->alert < 'A' || s->alert > 'E' || s->battery_percent < -1 || s->battery_percent > 100 ||
        !isfinite(s->total_ml) || s->total_ml < 0 || !isfinite(s->rate_mlh) || s->rate_mlh < 0 ||
        s->clock_hms[8] || s->clock_hms[2] != ':' || s->clock_hms[5] != ':') return false;
    for (unsigned i = 0; i < 8; i++) if (i != 2 && i != 5 && (s->clock_hms[i] < '0' || s->clock_hms[i] > '9')) return false;
    return (s->clock_hms[0] - '0') * 10 + s->clock_hms[1] - '0' <= 23 &&
           (s->clock_hms[3] - '0') * 10 + s->clock_hms[4] - '0' <= 59 &&
           (s->clock_hms[6] - '0') * 10 + s->clock_hms[7] - '0' <= 59;
}
void service_mobile_tick(void)
{
    if (!svc.mutex) return;
    mobile_link_peer_t peer; char session[65];
    lock(); if (!svc.running || !svc.peer) { unlock(); return; }
    peer = svc.peer; memcpy(session, svc.session_id, sizeof(session)); unlock();
    xSemaphoreTake(svc.ack_mutex, portMAX_DELAY);
    bool ready = hello_locked(peer, session);
    xSemaphoreGive(svc.ack_mutex);
    if (!ready) return;
    service_mobile_snapshot_t s; if (!svc.cfg.snapshot(&s, svc.cfg.ctx) || !snapshot_valid(&s)) return;
    uint64_t now = svc.cfg.monotonic_ms(svc.cfg.ctx);
    lock();
    if (svc.peer != peer || strcmp(session, svc.session_id) ||
        (!svc.dirty && s.state_seq == svc.last_state_seq && now - svc.last_send_ms < 1000)) { unlock(); return; }
    svc.dirty = false; svc.last_state_seq = s.state_seq; svc.last_send_ms = now; unlock();
    char msg[FRAME_MAX + 1], battery[16], checked[32];
    if (s.battery_percent < 0) memcpy(battery, "null", 5);
    else snprintf(battery, sizeof(battery), "%u", (unsigned)s.battery_percent);
    if (!s.storage_checked_known) memcpy(checked, "null", 5);
    else snprintf(checked, sizeof(checked), "%llu", (unsigned long long)s.storage_checked_uptime_ms);
    int n = snprintf(msg, sizeof(msg), "{\"v\":1,\"type\":\"status\",\"boot_id\":\"%s\",\"session_id\":\"%s\",\"seq\":%llu,\"state_seq\":%llu,\"sample_uptime_ms\":%llu,\"total_ml\":%.17g,\"rate_mlh\":%.17g,\"total_drops\":%llu,\"elapsed_s\":%llu,\"state\":\"%s\",\"alert\":\"%s\",\"battery_percent\":%s,\"storage_state\":\"%s\",\"storage_checked_uptime_ms\":%s,\"clock_hms\":\"%s\",\"time_synced\":%s}",
        svc.boot_id, session, (unsigned long long)s.seq, (unsigned long long)s.state_seq,
        (unsigned long long)s.sample_uptime_ms, s.total_ml, s.rate_mlh, (unsigned long long)s.total_drops,
        (unsigned long long)s.elapsed_s, states[s.state], (char[2]){s.alert, 0}, battery, storage[s.storage_state], checked, s.clock_hms, s.time_synced ? "true" : "false");
    xSemaphoreTake(svc.ack_mutex, portMAX_DELAY);
    lock(); bool current = svc.running && svc.peer == peer && !strcmp(session, svc.session_id); unlock();
    if (current && n > 0 && (size_t)n < sizeof(msg) && mobile_link_send_text(peer, msg, (size_t)n) != ERR_OK) {
        lock(); if (svc.peer == peer && !strcmp(session, svc.session_id)) svc.dirty = true; unlock();
    }
    xSemaphoreGive(svc.ack_mutex);
}

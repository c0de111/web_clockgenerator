#include "webserver.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"
#include "morse_player.h"
#include "signal_controller.h"
#include "webserver_pages.h"
#include "webserver_utils.h"

#include "lwip/tcp.h"

typedef struct {
    bool responded;
} web_connection_t;

static err_t webserver_accept(void *arg, struct tcp_pcb *pcb, err_t err);
static err_t webserver_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static void webserver_err(void *arg, err_t err);
static err_t webserver_sent(void *arg, struct tcp_pcb *pcb, u16_t len);
static void webserver_close(struct tcp_pcb *pcb, web_connection_t *state);
static void respond_with_form(struct tcp_pcb *pcb, web_connection_t *state);
static void handle_form_submission(const char *body);
static void handle_morse_submission(const char *body);
static void handle_morse_stop(void);
static void handle_morse_hold(const char *body);
static void respond_morse_status(struct tcp_pcb *pcb, web_connection_t *state);
static uint64_t clamp_frequency(uint64_t freq);
static bool parse_uint64(const char *value, uint64_t *out);
static bool parse_double(const char *value, double *out);
static bool extract_form_value(const char *body, const char *key, char *out, size_t out_len);
static int hex_digit_value(char c);
static void format_step_text(double step, char *buf, size_t len) {
    double rounded = round(step);
    if (fabs(step - rounded) < 1e-6) {
        snprintf(buf, len, "%.0f", rounded);
    } else {
        snprintf(buf, len, "%.6f", step);
        size_t n = strlen(buf);
        while (n > 0 && buf[n - 1] == '0') {
            buf[--n] = '\0';
        }
        if (n > 0 && buf[n - 1] == '.') {
            buf[--n] = '\0';
        }
    }
}

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static bool extract_form_value(const char *body, const char *key, char *out, size_t out_len) {
    if (!body || !key || !out || out_len == 0) {
        return false;
    }

    const char *key_pos = strstr(body, key);
    if (!key_pos) {
        out[0] = '\0';
        return false;
    }

    key_pos += strlen(key);
    size_t idx = 0;

    while (*key_pos && *key_pos != '&' && idx < out_len - 1) {
        char c = *key_pos++;
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && key_pos[0] && key_pos[1]) {
            int hi = hex_digit_value(key_pos[0]);
            int lo = hex_digit_value(key_pos[1]);
            if (hi >= 0 && lo >= 0) {
                c = (char)((hi << 4) | lo);
                key_pos += 2;
            }
        }
        out[idx++] = c;
    }

    out[idx] = '\0';
    return true;
}

static char g_status_message[128] = "";
static bool g_status_is_error = false;
static char g_status_prev_message[128] = "";
static bool g_status_prev_is_error = false;
static bool g_status_prev_valid = false;
static bool g_morse_hold_active = false;
static bool g_morse_hold_prev_enabled = false;

void webserver_init(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        log_error("Failed to allocate TCP PCB for webserver");
        return;
    }

    const u16_t port = 80;
    err_t err = tcp_bind(pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        log_error("tcp_bind failed on port %u: %d", port, err);
        tcp_close(pcb);
        return;
    }

    pcb = tcp_listen_with_backlog(pcb, 2);
    tcp_accept(pcb, webserver_accept);
    log_info("Webserver listening on port %u", port);
}

void webserver_set_status(const char *message, bool is_error) {
    if (!message || !*message) {
        g_status_message[0] = '\0';
        g_status_is_error = false;
        return;
    }

    snprintf(g_status_message, sizeof(g_status_message), "%s", message);
    g_status_is_error = is_error;
}

static err_t webserver_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    if (err != ERR_OK || !pcb) {
        return ERR_VAL;
    }

    web_connection_t *state = calloc(1, sizeof(web_connection_t));
    if (!state) {
        log_error("Out of memory allocating connection state");
        tcp_arg(pcb, NULL);
        return ERR_MEM;
    }

    tcp_arg(pcb, state);
    tcp_recv(pcb, webserver_recv);
    tcp_err(pcb, webserver_err);
    tcp_sent(pcb, webserver_sent);

    return ERR_OK;
}

static err_t webserver_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    web_connection_t *state = (web_connection_t *)arg;

    if (!pcb) {
        if (p) {
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (err != ERR_OK) {
        if (p) {
            pbuf_free(p);
        }
        webserver_close(pcb, state);
        return err;
    }

    if (!p) {
        webserver_close(pcb, state);
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);

    char request[1024] = {0};
    const u16_t copy_len = p->tot_len < sizeof(request) - 1 ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, copy_len, 0);

    pbuf_free(p);

    if (!state->responded) {
        if (strncmp(request, "GET ", 4) == 0) {
            const char *path_start = request + 4;
            const char *path_end = strchr(path_start, ' ');
            if (path_end) {
                size_t path_len = (size_t)(path_end - path_start);
                if (path_len == strlen("/morse/status") &&
                    strncmp(path_start, "/morse/status", path_len) == 0) {
                    respond_morse_status(pcb, state);
                    return ERR_OK;
                }
            }
        } else if (strncmp(request, "POST ", 5) == 0) {
            const char *path_start = request + 5;
            const char *path_end = strchr(path_start, ' ');
            if (path_end) {
                size_t path_len = (size_t)(path_end - path_start);
                const char *body = strstr(request, "\r\n\r\n");
                if (body) {
                    body += 4;
                }

                if (path_len == strlen("/signal") &&
                    strncmp(path_start, "/signal", path_len) == 0) {
                    if (body) {
                        handle_form_submission(body);
                    }
                } else if (path_len == strlen("/morse") &&
                           strncmp(path_start, "/morse", path_len) == 0) {
                    if (body) {
                        handle_morse_submission(body);
                    }
                } else if (path_len == strlen("/morse/stop") &&
                           strncmp(path_start, "/morse/stop", path_len) == 0) {
                    handle_morse_stop();
                } else if (path_len == strlen("/morse/hold") &&
                           strncmp(path_start, "/morse/hold", path_len) == 0) {
                    if (body) {
                        handle_morse_hold(body);
                    }
                }
            }
        }

        respond_with_form(pcb, state);
    } else {
        webserver_close(pcb, state);
    }
    return ERR_OK;
}

static void respond_with_form(struct tcp_pcb *pcb, web_connection_t *state) {
    char page[16384];
    signal_state_t current = signal_controller_get_state();
    char morse_text[MORSE_MAX_CHARS + 1] = {0};
    uint16_t morse_wpm = 0;
    int16_t morse_fwpm = -1;
    morse_get_form_defaults(morse_text, sizeof(morse_text), &morse_wpm, &morse_fwpm);

    webserver_build_landing_page(page, sizeof(page), current.frequency_hz, current.drive_ma,
                                 current.output_enabled, g_status_message, g_status_is_error,
                                 morse_text, morse_wpm, morse_fwpm, morse_is_playing(),
                                 morse_status_text(), g_morse_hold_active);

    if (webserver_send_response(pcb, page) == ERR_OK) {
        state->responded = true;
    } else {
        webserver_close(pcb, state);
    }
}

static bool parse_uint64(const char *value, uint64_t *out) {
    if (!value || !*value) {
        return false;
    }

    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value || (*end != '\0' && *end != '&')) {
        return false;
    }

    *out = (uint64_t)parsed;
    return true;
}

static err_t webserver_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg;
    (void)len;
    if (!pcb) {
        return ERR_OK;
    }
    tcp_close(pcb);
    return ERR_OK;
}

static void webserver_err(void *arg, err_t err) {
    (void)err;
    web_connection_t *state = (web_connection_t *)arg;
    free(state);
}

static void webserver_close(struct tcp_pcb *pcb, web_connection_t *state) {
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_close(pcb);
    }
    free(state);
}

static uint64_t clamp_frequency(uint64_t freq) {
    if (freq < 8000) {
        return 8000;
    }
    if (freq > 200000000) {
        return 200000000;
    }
    return freq;
}

static void handle_form_submission(const char *body) {
    char action_buf[32] = {0};
    extract_form_value(body, "action=", action_buf, sizeof(action_buf));

    if (strcmp(action_buf, "toggle-output") == 0) {
        if (g_morse_hold_active) {
            webserver_set_status("Output locked for Morse", true);
            return;
        }
        signal_state_t current = signal_controller_get_state();
        bool desired = !current.output_enabled;
        if (signal_controller_enable_output(desired)) {
            webserver_set_status(desired ? "Output enabled" : "Output disabled", false);
        } else {
            webserver_set_status("Error: failed to toggle output", true);
        }
        return;
    }

    char freq_buf[32] = {0};
    char drive_buf[8] = {0};

    bool freq_found = extract_form_value(body, "frequency=", freq_buf, sizeof(freq_buf));
    bool drive_found = extract_form_value(body, "drive=", drive_buf, sizeof(drive_buf));

    signal_state_t previous = signal_controller_get_state();

    uint64_t freq = 0;
    uint64_t drive_val = 0;

    bool freq_ok = freq_found && parse_uint64(freq_buf, &freq);
    bool drive_ok = drive_found && parse_uint64(drive_buf, &drive_val);

    if (!freq_ok || !drive_ok) {
        webserver_set_status("Error: invalid form data", true);
        log_error("[USER] invalid form data (freq='%s', drive='%s')", freq_buf, drive_buf);
        return;
    }

    freq = clamp_frequency(freq);

    if (!(drive_val == 2 || drive_val == 4 || drive_val == 6 || drive_val == 8)) {
        webserver_set_status("Error: drive must be 2, 4, 6 or 8 mA", true);
        log_error("[USER] drive out of range: %llu", (unsigned long long)drive_val);
        return;
    }

    if (!signal_controller_set(freq, (uint8_t)drive_val)) {
        webserver_set_status("Error: failed to program Si5351", true);
        return;
    }

    bool freq_changed = (previous.frequency_hz != freq) || (previous.drive_ma != drive_val);

    char status[128];
    if (freq_changed) {
        snprintf(status, sizeof(status), "Applied %llu Hz @ %u mA", (unsigned long long)freq,
                 (unsigned)drive_val);
    } else {
        strcpy(status, "No parameter change");
    }
    webserver_set_status(status, false);
}

static void handle_morse_submission(const char *body) {
    char text_buf[MORSE_MAX_CHARS * 3] = {0};
    char wpm_buf[8] = {0};
    char fwpm_buf[8] = {0};

    extract_form_value(body, "text=", text_buf, sizeof(text_buf));
    extract_form_value(body, "wpm=", wpm_buf, sizeof(wpm_buf));
    extract_form_value(body, "fwpm=", fwpm_buf, sizeof(fwpm_buf));

    size_t text_len = strlen(text_buf);
    if (text_len == 0) {
        webserver_set_status("Error: text is required", true);
        return;
    }
    if (text_len > MORSE_MAX_CHARS) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Error: text must be %u characters or fewer",
                 (unsigned)MORSE_MAX_CHARS);
        webserver_set_status(msg, true);
        return;
    }

    char *end = NULL;
    long wpm_long = strtol(wpm_buf, &end, 10);
    if (end == wpm_buf || wpm_long < 1 || wpm_long > 1000) {
        webserver_set_status("Error: WPM must be 1-1000", true);
        return;
    }

    int farnsworth = -1;
    if (fwpm_buf[0]) {
        end = NULL;
        long fwpm_long = strtol(fwpm_buf, &end, 10);
        if (end == fwpm_buf || fwpm_long < 1 || fwpm_long > wpm_long) {
            webserver_set_status("Error: Farnsworth must be 1-<=WPM", true);
            return;
        }
        farnsworth = (int)fwpm_long;
    }

    if (morse_is_playing()) {
        webserver_set_status("Morse playback busy", true);
        return;
    }

    if (!morse_start(text_buf, (uint8_t)text_len, (uint16_t)wpm_long, (int16_t)farnsworth)) {
        const char *err = morse_last_error();
        if (err && *err) {
            webserver_set_status(err, true);
        } else {
            webserver_set_status("Error: failed to start Morse playback", true);
        }
        return;
    }

    if (!g_morse_hold_active) {
        webserver_set_status("Morse playback started", false);
    }
}

static void handle_morse_stop(void) {
    if (morse_is_playing()) {
        morse_stop();
        if (!g_morse_hold_active) {
            webserver_set_status("Stop requested", false);
        }
    } else if (!g_morse_hold_active) {
        webserver_set_status("Morse playback idle", false);
    }
}

static void handle_morse_hold(const char *body) {
    char active_buf[8] = {0};
    extract_form_value(body, "active=", active_buf, sizeof(active_buf));
    bool activate = (active_buf[0] == '1' || active_buf[0] == 't' || active_buf[0] == 'T');

    if (activate) {
        if (!g_morse_hold_active) {
            signal_state_t state = signal_controller_get_state();
            g_morse_hold_prev_enabled = state.output_enabled;
            if (state.output_enabled) {
                signal_controller_enable_output(false);
            }
            if (g_status_message[0]) {
                snprintf(g_status_prev_message, sizeof(g_status_prev_message), "%s",
                         g_status_message);
                g_status_prev_is_error = g_status_is_error;
                g_status_prev_valid = true;
            } else {
                g_status_prev_valid = false;
            }
        }
        g_morse_hold_active = true;
        webserver_set_status("Morse mode", false);
    } else {
        if (g_morse_hold_active && g_morse_hold_prev_enabled) {
            signal_controller_enable_output(true);
        }
        g_morse_hold_active = false;
        g_morse_hold_prev_enabled = false;
        if (g_status_prev_valid) {
            webserver_set_status(g_status_prev_message, g_status_prev_is_error);
        } else {
            webserver_set_status(NULL, false);
        }
        g_status_prev_valid = false;
    }
}

static void respond_morse_status(struct tcp_pcb *pcb, web_connection_t *state) {
    if (!pcb) {
        if (state) {
            free(state);
        }
        return;
    }

    bool playing = morse_is_playing();
    const char *status = morse_status_text();
    if (!status || !*status) {
        status = "Idle";
    }

    signal_state_t sig_state = signal_controller_get_state();

    char body[192];
    int body_len = snprintf(
        body, sizeof(body), "{\"playing\":%s,\"status\":\"%s\",\"hold\":%s,\"output_enabled\":%s}",
        playing ? "true" : "false", status, g_morse_hold_active ? "true" : "false",
        sig_state.output_enabled ? "true" : "false");
    if (body_len < 0 || body_len >= (int)sizeof(body)) {
        const char fallback[] =
            "{\"playing\":false,\"status\":\"Idle\",\"hold\":false,\"output_enabled\":false}";
        memcpy(body, fallback, sizeof(fallback));
        body_len = (int)sizeof(fallback) - 1;
    }

    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json; charset=utf-8\r\n"
                              "Cache-Control: no-store\r\n"
                              "Content-Length: %d\r\n"
                              "Connection: close\r\n\r\n",
                              body_len);

    if (header_len > 0 && header_len < (int)sizeof(header)) {
        err_t err = tcp_write(pcb, header, (u16_t)header_len, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            err = tcp_write(pcb, body, (u16_t)body_len, TCP_WRITE_FLAG_COPY);
            if (err == ERR_OK) {
                tcp_output(pcb);
            }
        }
    }

    webserver_close(pcb, state);
}

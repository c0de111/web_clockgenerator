#include "webserver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "logging.h"
#include "webserver_pages.h"
#include "webserver_utils.h"
#include "signal_controller.h"

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
static uint64_t clamp_frequency(uint64_t freq);
static bool parse_uint64(const char *value, uint64_t *out);
static bool parse_double(const char *value, double *out);
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

static char g_status_message[128] = "";
static bool g_status_is_error = false;

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
        if (strncmp(request, "POST ", 5) == 0) {
            const char *path_start = request + 5;
            const char *path_end = strchr(path_start, ' ');
            if (path_end && strncmp(path_start, "/signal", (size_t)(path_end - path_start)) == 0) {
                const char *body = strstr(request, "\r\n\r\n");
                if (body) {
                    body += 4;
                    handle_form_submission(body);
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
    char page[8192];
    signal_state_t current = signal_controller_get_state();
    webserver_build_landing_page(page, sizeof(page), current.frequency_hz, current.drive_ma,
                                 g_status_message, g_status_is_error);

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
    char freq_buf[32] = {0};
    char drive_buf[8] = {0};

    const char *freq_key = strstr(body, "frequency=");
    if (freq_key) {
        freq_key += strlen("frequency=");
        size_t i = 0;
        while (freq_key[i] && freq_key[i] != '&' && i < sizeof(freq_buf) - 1) {
            freq_buf[i] = freq_key[i];
            ++i;
        }
        freq_buf[i] = '\0';
    }

    const char *drive_key = strstr(body, "drive=");
    if (drive_key) {
        drive_key += strlen("drive=");
        size_t i = 0;
        while (drive_key[i] && drive_key[i] != '&' && i < sizeof(drive_buf) - 1) {
            drive_buf[i] = drive_key[i];
            ++i;
        }
        drive_buf[i] = '\0';
    }

    signal_state_t previous = signal_controller_get_state();

    uint64_t freq = 0;
    uint64_t drive_val = 0;

    bool freq_ok = parse_uint64(freq_buf, &freq);
    bool drive_ok = parse_uint64(drive_buf, &drive_val);

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
        snprintf(status, sizeof(status), "Applied %llu Hz @ %u mA",
                 (unsigned long long)freq, (unsigned)drive_val);
    } else {
        strcpy(status, "No parameter change");
    }
    webserver_set_status(status, false);
}

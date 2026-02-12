#include "webserver_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"

#define TCP_CHUNK_SIZE 1024

typedef struct {
    struct tcp_pcb *pcb;
    const char *cursor;
    size_t remaining;
    char *body_copy;
} web_response_state_t;

static err_t webserver_send_next_chunk(void *arg, struct tcp_pcb *pcb, u16_t len);
static err_t webserver_poll_callback(void *arg, struct tcp_pcb *pcb);
static void webserver_response_free(web_response_state_t *state);

err_t webserver_send_response(struct tcp_pcb *pcb, const char *body) {
    if (!pcb || !body) {
        return ERR_VAL;
    }

    const size_t body_len = strlen(body);
    char *copy = NULL;
    if (body_len > 0) {
        copy = (char *)malloc(body_len);
        if (!copy) {
            log_error("Failed to allocate response buffer (%zu bytes)", body_len);
            return ERR_MEM;
        }
        memcpy(copy, body, body_len);
    }

    char header[192];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              body_len);
    if (header_len <= 0 || header_len >= (int)sizeof(header)) {
        free(copy);
        log_error("Failed to build HTTP header");
        return ERR_MEM;
    }

    err_t err = tcp_write(pcb, header, header_len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        log_error("tcp_write header failed: %d", err);
        free(copy);
        return err;
    }

    web_response_state_t *state = (web_response_state_t *)calloc(1, sizeof(web_response_state_t));
    if (!state) {
        log_error("Failed to allocate response state");
        free(copy);
        return ERR_MEM;
    }

    state->pcb = pcb;
    state->cursor = copy;
    state->remaining = body_len;
    state->body_copy = copy;

    tcp_arg(pcb, state);
    tcp_sent(pcb, webserver_send_next_chunk);
    tcp_poll(pcb, webserver_poll_callback, 2);

    return webserver_send_next_chunk(state, pcb, 0);
}

static err_t webserver_send_next_chunk(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)len;

    web_response_state_t *state = (web_response_state_t *)arg;
    if (!state || !pcb) {
        return ERR_OK;
    }

    if (state->remaining == 0) {
        err_t flush_err = tcp_output(pcb);
        if (flush_err != ERR_OK) {
            log_warn("tcp_output flush failed: %d", flush_err);
        }
        webserver_response_free(state);
        return ERR_OK;
    }

    u16_t sndbuf = tcp_sndbuf(pcb);
    if (sndbuf == 0) {
        return ERR_OK;
    }

    u16_t chunk = (state->remaining > TCP_CHUNK_SIZE) ? TCP_CHUNK_SIZE : (u16_t)state->remaining;
    if (chunk > sndbuf) {
        chunk = sndbuf;
    }

    if (chunk == 0) {
        return ERR_OK;
    }

    u16_t flags = TCP_WRITE_FLAG_COPY;
    if (chunk < state->remaining) {
        flags |= TCP_WRITE_FLAG_MORE;
    }

    err_t err = tcp_write(pcb, state->cursor, chunk, flags);
    if (err != ERR_OK) {
        if (err == ERR_MEM) {
            return ERR_OK;
        }
        log_error("tcp_write chunk failed: %d", err);
        webserver_response_free(state);
        return err;
    }

    state->cursor += chunk;
    state->remaining -= chunk;

    err = tcp_output(pcb);
    if (err != ERR_OK) {
        log_warn("tcp_output returned %d after chunk", err);
    }

    return ERR_OK;
}

static void webserver_response_free(web_response_state_t *state) {
    if (!state) {
        return;
    }
    tcp_arg(state->pcb, NULL);
    tcp_sent(state->pcb, NULL);
    tcp_poll(state->pcb, NULL, 0);
    free(state->body_copy);
    free(state);
}

static err_t webserver_poll_callback(void *arg, struct tcp_pcb *pcb) {
    return webserver_send_next_chunk(arg, pcb, 0);
}

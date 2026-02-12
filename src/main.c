#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/stdio_usb.h"

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "logging.h"
#include "webserver.h"
#include "signal_controller.h"
#include "morse_player.h"

static void start_dhcp_server(void);
static void dhcp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port);
static bool wait_for_usb_connection(uint32_t timeout_ms);

static const uint8_t dhcp_offer_template[] = {
    0x02, 0x01, 0x06, 0x00,                  // BOOTP: op, htype, hlen, hops
    0x00, 0x00, 0x00, 0x00,                  // XID (Transaction ID)
    0x00, 0x00, 0x00, 0x00,                  // SECS, FLAGS
    0, 0, 0, 0,                              // CIADDR (Client IP)
    192, 168, 4, 100,                        // YIADDR (Your IP)
    192, 168, 4, 1,                          // SIADDR (Server IP)
    0x00, 0x00, 0x00, 0x00,                  // GIADDR (Gateway IP)
    // CHADDR (Client HW addr)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // CHADDR padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // SNAME (64 bytes)
    [44] = 0,
    [107] = 0,
    // FILE (128 bytes)
    [108] = 0,
    [235] = 0,
    // MAGIC COOKIE
    99, 130, 83, 99,
    // DHCP Options
    53, 1, 2,                                // Offer
    54, 4, 192, 168, 4, 1,                   // Server Identifier
    51, 4, 0x00, 0x01, 0x51, 0x80,           // Lease time = 86400
    58, 4, 0x00, 0x00, 0x01, 0x2C,           // Renewal (T1) = 300s
    59, 4, 0x00, 0x00, 0x01, 0xE0,           // Rebinding (T2) = 480s
    1, 4, 255, 255, 255, 0,                  // Subnet mask
    3, 4, 192, 168, 4, 1,                    // Router
    6, 4, 192, 168, 4, 1,                    // DNS
    255
};

static const uint8_t dhcp_ack_template[] = {
    0x02, 0x01, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0, 0, 0, 0,
    192, 168, 4, 100,
    192, 168, 4, 1,
    0x00, 0x00, 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    [44] = 0,
    [107] = 0,
    [108] = 0,
    [235] = 0,
    99, 130, 83, 99,
    53, 1, 5,                                // ACK
    54, 4, 192, 168, 4, 1,
    51, 4, 0x00, 0x01, 0x51, 0x80,
    58, 4, 0x00, 0x00, 0x01, 0x2C,
    59, 4, 0x00, 0x00, 0x01, 0xE0,
    1, 4, 255, 255, 255, 0,
    3, 4, 192, 168, 4, 1,
    6, 4, 192, 168, 4, 1,
    255
};

int main(void) {
    stdio_init_all();
    if (wait_for_usb_connection(2000)) {
        printf("USB connected\n");
    } else {
        printf("USB timeout\n");
    }
    logging_init();

    log_info("Clock generator web firmware booting");

    if (!signal_controller_init()) {
        log_warn("Si5351 init failed; outputs will remain inactive");
        webserver_set_status("Si5351 not found - check hardware", true);
    } else {
        webserver_set_status(NULL, false);
    }

    if (cyw43_arch_init_with_country(CYW43_COUNTRY_WORLDWIDE)) {
        log_error("Failed to initialize CYW43");
        return 1;
    }

    const char *ssid = "clockgen";
    const char *password = "12345678";

    cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);

    ip4_addr_t ip, netmask, gw;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 4, 1);

    struct netif *netif = netif_default;
    if (netif) {
        netif_set_addr(netif, &ip, &netmask, &gw);
    }

    start_dhcp_server();
    webserver_init();

    log_info("Access point ready: SSID=%s, IP=192.168.4.1", ssid);

    while (true) {
        cyw43_arch_poll();
        morse_tick();
        sleep_ms(5);
    }
}

static bool wait_for_usb_connection(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        if (stdio_usb_connected()) {
            return true;
        }
        sleep_ms(10);
    }
    return stdio_usb_connected();
}

static void start_dhcp_server(void) {
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        log_error("Failed to allocate UDP PCB for DHCP");
        return;
    }

    if (udp_bind(pcb, IP_ADDR_ANY, 67) != ERR_OK) {
        log_error("DHCP bind failed");
        udp_remove(pcb);
        return;
    }

    udp_recv(pcb, dhcp_recv_cb, NULL);
    log_info("DHCP server listening on port 67");
}

static void dhcp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port) {
    (void)arg;
    if (!p || p->len < 240) {
        if (p) {
            pbuf_free(p);
        }
        return;
    }

    const uint8_t *request = (const uint8_t *)p->payload;

    uint8_t msg_type = 0;
    for (int i = 240; i < p->len - 2; i++) {
        if (request[i] == 53 && request[i + 1] == 1) {
            msg_type = request[i + 2];
            break;
        }
    }

    const uint8_t *template = NULL;
    size_t template_len = 0;

    switch (msg_type) {
        case 1: // Discover
            template = dhcp_offer_template;
            template_len = sizeof(dhcp_offer_template);
            break;
        case 3: // Request
            template = dhcp_ack_template;
            template_len = sizeof(dhcp_ack_template);
            break;
        default:
            pbuf_free(p);
            return;
    }

    uint8_t response[300] = {0};
    memcpy(response, template, template_len);
    memcpy(response + 4, request + 4, 4);     // XID
    memcpy(response + 28, request + 28, 16);  // CHADDR

    struct pbuf *resp_buf = pbuf_alloc(PBUF_TRANSPORT, template_len, PBUF_RAM);
    if (!resp_buf) {
        pbuf_free(p);
        return;
    }

    memcpy(resp_buf->payload, response, template_len);
    udp_sendto(pcb, resp_buf, addr, port);

    pbuf_free(resp_buf);
    pbuf_free(p);
}

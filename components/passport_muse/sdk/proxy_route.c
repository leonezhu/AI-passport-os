#include "proxy_route.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "lwip/inet.h"
#include "lwip/sockets.h"
#else
#include <arpa/inet.h>
#endif

bool muse_proxy_ipv4_ok(const char *host)
{
    struct in_addr address;
    if (!host || strlen(host) >= MUSE_PROXY_HOST_MAX || inet_pton(AF_INET, host, &address) != 1) return false;
    uint32_t ip = ntohl(address.s_addr);
    return ip != 0 && (ip >> 24) != 127 && (ip >> 24) < 224 && (ip & 255) != 255;
}

bool muse_hotspot_valid(const char *ssid, uint16_t port)
{
    if (!ssid) return false;
    if (!*ssid) return port == 0;
    return port != 0 && strlen(ssid) <= MUSE_HOTSPOT_SSID_MAX;
}

bool muse_hotspot_format(char *out, size_t cap, const char *ssid, uint16_t port)
{
    if (!cap || !muse_hotspot_valid(ssid, port)) return false;
    int n = *ssid ? snprintf(out, cap, "%u:%s", port, ssid) : snprintf(out, cap, "%s", "");
    return n >= 0 && (size_t)n < cap;
}

bool muse_hotspot_parse(const char *stored, char *ssid, size_t cap, uint16_t *port)
{
    if (cap) ssid[0] = 0;
    if (port) *port = 0;
    if (!stored || !*stored) return true;
    const char *colon = strchr(stored, ':');
    if (!colon || colon == stored) return false;
    char *end;
    long n = strtol(stored, &end, 10);
    if (end != colon || n < 1 || n > 65535) return false;
    const char *name = colon + 1;
    if (!muse_hotspot_valid(name, (uint16_t)n) || !*name || strlen(name) >= cap) return false;
    memcpy(ssid, name, strlen(name) + 1);
    if (port) *port = (uint16_t)n;
    return true;
}

bool muse_proxy_route(const char *current_ssid, const char *gateway,
                      const char *hotspot_ssid, uint16_t hotspot_port,
                      const char *home_host, uint16_t home_port,
                      char *host, uint16_t *port)
{
    bool hotspot = current_ssid && hotspot_ssid && *hotspot_ssid && hotspot_port &&
                   strcmp(current_ssid, hotspot_ssid) == 0 && muse_proxy_ipv4_ok(gateway);
    const char *chosen = hotspot ? gateway : (home_host ? home_host : "");
    snprintf(host, MUSE_PROXY_HOST_MAX, "%s", chosen);
    *port = hotspot ? hotspot_port : (*host ? home_port : 0);
    return hotspot;
}

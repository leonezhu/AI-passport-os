/*
 * Picks the HTTP CONNECT proxy for the network the device is on. At home the
 * saved computer proxy is used. On the user's named phone hotspot the phone
 * itself is the proxy: its address is the DHCP gateway, which Android may
 * change between sessions, so only the SSID and port are stored.
 * Pure logic; no ESP-IDF dependencies so the host tests can cover it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUSE_HOTSPOT_SSID_MAX 32
#define MUSE_PROXY_HOST_MAX 16

/* Literal unicast IPv4 a LAN proxy could listen on. */
bool muse_proxy_ipv4_ok(const char *host);

/* "" with port 0 turns the rule off; otherwise 1..32 SSID bytes, port 1..65535. */
bool muse_hotspot_valid(const char *ssid, uint16_t port);

/* Stored as "<port>:<ssid>" so an SSID containing ':' still round-trips. */
bool muse_hotspot_format(char *out, size_t cap, const char *ssid, uint16_t port);
bool muse_hotspot_parse(const char *stored, char *ssid, size_t cap, uint16_t *port);

/* Writes the proxy for the current network into host/port (host has
 * MUSE_PROXY_HOST_MAX bytes). Returns true when the phone hotspot rule
 * applied; otherwise copies the home proxy, which may be empty (direct). */
bool muse_proxy_route(const char *current_ssid, const char *gateway,
                      const char *hotspot_ssid, uint16_t hotspot_port,
                      const char *home_host, uint16_t home_port,
                      char *host, uint16_t *port);

#ifdef __cplusplus
}
#endif

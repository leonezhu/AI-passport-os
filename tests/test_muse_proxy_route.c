#include <assert.h>
#include <string.h>
#include "proxy_route.h"

int main(void) {
    char ssid[MUSE_HOTSPOT_SSID_MAX + 1], host[MUSE_PROXY_HOST_MAX], stored[48];
    uint16_t port = 0;

    assert(muse_hotspot_valid("", 0) && !muse_hotspot_valid("", 7890));
    assert(muse_hotspot_valid("我的 iPhone", 7890) && !muse_hotspot_valid("我的 iPhone", 0));
    assert(muse_hotspot_valid("12345678901234567890123456789012", 1));
    assert(!muse_hotspot_valid("123456789012345678901234567890123", 1));

    // SSIDs may contain ':' and spaces; the port prefix keeps parsing unambiguous.
    assert(muse_hotspot_format(stored, sizeof(stored), "Enric’s iPhone: 5G ", 7890));
    assert(!strcmp(stored, "7890:Enric’s iPhone: 5G "));
    assert(muse_hotspot_parse(stored, ssid, sizeof(ssid), &port));
    assert(!strcmp(ssid, "Enric’s iPhone: 5G ") && port == 7890);
    assert(muse_hotspot_format(stored, sizeof(stored), "", 0) && !strcmp(stored, ""));
    assert(muse_hotspot_parse("", ssid, sizeof(ssid), &port) && !ssid[0] && port == 0);
    const char *bad[] = {"7890", ":phone", "0:phone", "65536:phone", "x1:phone", "7890:",
                         "1:123456789012345678901234567890123"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!muse_hotspot_parse(bad[i], ssid, sizeof(ssid), &port));
        assert(!ssid[0] && port == 0);
    }

    assert(muse_proxy_ipv4_ok("172.20.10.1") && muse_proxy_ipv4_ok("192.168.43.1"));
    assert(!muse_proxy_ipv4_ok("") && !muse_proxy_ipv4_ok("0.0.0.0") && !muse_proxy_ipv4_ok("127.0.0.1"));
    assert(!muse_proxy_ipv4_ok("192.168.1.255") && !muse_proxy_ipv4_ok("224.0.0.1") && !muse_proxy_ipv4_ok("phone.local"));

    // On the named hotspot the phone gateway is the proxy, whatever its subnet.
    assert(muse_proxy_route("我的 iPhone", "172.20.10.1", "我的 iPhone", 7890, "192.168.5.4", 7897, host, &port));
    assert(!strcmp(host, "172.20.10.1") && port == 7890);
    assert(muse_proxy_route("Pixel", "10.108.25.1", "Pixel", 7891, "", 0, host, &port));
    assert(!strcmp(host, "10.108.25.1") && port == 7891);
    // Home Wi-Fi and every other network keep the computer proxy (or direct).
    assert(!muse_proxy_route("Home", "192.168.5.1", "我的 iPhone", 7890, "192.168.5.4", 7897, host, &port));
    assert(!strcmp(host, "192.168.5.4") && port == 7897);
    assert(!muse_proxy_route("Hotel", "10.0.0.1", "我的 iPhone", 7890, "", 0, host, &port));
    assert(!host[0] && port == 0);
    // Matching is exact, and a missing gateway never produces a bogus proxy.
    assert(!muse_proxy_route("我的 iphone", "172.20.10.1", "我的 iPhone", 7890, "192.168.5.4", 7897, host, &port));
    assert(!muse_proxy_route("我的 iPhone", "", "我的 iPhone", 7890, "192.168.5.4", 7897, host, &port));
    assert(!strcmp(host, "192.168.5.4") && port == 7897);
    // Rule off: an empty current SSID must not match an empty rule.
    assert(!muse_proxy_route("", "192.168.5.1", "", 0, "192.168.5.4", 7897, host, &port));
    assert(!strcmp(host, "192.168.5.4") && port == 7897);
    return 0;
}

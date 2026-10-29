/* Prints the port of each Wireless debugging service of this phone, one
 * per line. Apps may neither list network interfaces nor send to the mDNS
 * group, but a UDP socket connected to the group gives the Wi-Fi address,
 * and Android's mDNS responder answers a plain DNS query (legacy unicast)
 * sent to that address with the PTR records and their SRV records. */

#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>

/* id 0, no flags, one question: _adb-tls-connect._tcp.local PTR IN */
static const unsigned char query[] =
    "\0\0\0\0\0\1\0\0\0\0\0\0"
    "\x10_adb-tls-connect\4_tcp\5local\0"
    "\0\x0c\0\1";

/* Returns the offset after the name at off, or n if it does not fit. */
static int skip_name(const unsigned char *p, int n, int off)
{
    while (off < n) {
        if (p[off] == 0)
            return off + 1;
        if ((p[off] & 0xc0) == 0xc0)
            return off + 2;
        off += 1 + p[off];
    }
    return n;
}

int main(void)
{
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(5353) };
    socklen_t len = sizeof a;
    struct timeval timeout = { .tv_sec = 2 };
    unsigned char p[9000];
    char ip[INET_ADDRSTRLEN];
    int s, n, off, records, found = 0;

    inet_pton(AF_INET, "224.0.0.251", &a.sin_addr);
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0 || connect(s, (struct sockaddr *)&a, sizeof a) ||
        getsockname(s, (struct sockaddr *)&a, &len)) {
        perror("droid-nix-adb-connect: no Wi-Fi address");
        return 1;
    }
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);

    a.sin_port = htons(5353);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    if (connect(s, (struct sockaddr *)&a, sizeof a) == 0 &&
        send(s, query, sizeof query - 1, 0) == sizeof query - 1 &&
        (n = recv(s, p, sizeof p, 0)) >= 12) {
        off = skip_name(p, n, 12) + 4;
        records = (p[6] << 8 | p[7]) + (p[8] << 8 | p[9]) + (p[10] << 8 | p[11]);
        for (; records > 0; records--) {
            off = skip_name(p, n, off);
            if (off + 10 > n)
                break;
            /* SRV: priority, weight, port, target */
            if ((p[off] << 8 | p[off + 1]) == 33 && off + 16 <= n) {
                printf("%d\n", p[off + 14] << 8 | p[off + 15]);
                found = 1;
            }
            off += 10 + (p[off + 8] << 8 | p[off + 9]);
        }
    }
    if (!found) {
        fprintf(stderr, "No Wireless debugging found on %s: turn it on in "
                "the developer options; pair once with adb pair.\n", ip);
        return 1;
    }
    return 0;
}

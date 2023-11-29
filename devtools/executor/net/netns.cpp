/*
 * from https://github.com/iffyio/isolate/blob/master/isolate.c
 */
#include "netns.h"

#if defined(_linux_)
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <util/system/yassert.h>
#endif

namespace NNetNs {
#if defined(_linux_)
    int CreateSocket(int domain, int type, int protocol) {
        int sock_fd = socket(domain, type, protocol);

        Y_ASSERT(sock_fd >= 0 && "cannot open socket");

        return sock_fd;
    }

    void IfUp(const TString& ifname, const TString& ip, const TString& netmask) {
        int sock_fd = CreateSocket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(struct ifreq));
        strncpy(ifr.ifr_name, ifname.c_str(), ifname.length());

        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(struct sockaddr_in));
        saddr.sin_family = AF_INET;
        saddr.sin_port = 0;

        char *p = (char *) &saddr;

        saddr.sin_addr.s_addr = inet_addr(ip.c_str());
        memcpy(((char *) &(ifr.ifr_addr)), p, sizeof(struct sockaddr));
        Y_ASSERT(!ioctl(sock_fd, SIOCSIFADDR, &ifr) && "cannot set ip addr");

        saddr.sin_addr.s_addr = inet_addr(netmask.c_str());
        memcpy(((char *) &(ifr.ifr_addr)), p, sizeof(struct sockaddr));
        Y_ASSERT(!ioctl(sock_fd, SIOCSIFNETMASK, &ifr) && "cannot set ip addr");

        ifr.ifr_flags |= IFF_UP | IFF_BROADCAST |
                         IFF_RUNNING | IFF_MULTICAST;

        Y_ASSERT(!ioctl(sock_fd, SIOCSIFFLAGS, &ifr) && "cannot set flags for addr");
        close(sock_fd);
    }
#endif
}

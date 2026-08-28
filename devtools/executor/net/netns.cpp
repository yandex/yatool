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

    #include <util/generic/yexception.h>
    #include <util/system/file.h>
#endif

namespace NNetNs {
#if defined(_linux_)
    namespace {
        void IoctlOrThrow(int fd, unsigned long request, struct ifreq* ifr, TStringBuf action) {
            if (ioctl(fd, request, ifr) != 0) {
                ythrow TSystemError() << action;
            }
        }
    } // namespace

    void IfUp(const TString& ifname, const TString& ip, const TString& netmask) {
        const int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (fd < 0) {
            ythrow TSystemError() << "Cannot open network namespace control socket";
        }
        TFile socketFile(fd, "network namespace control socket");

        Y_ENSURE(ifname.size() < IFNAMSIZ, "Network interface name is too long: " << ifname);
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(struct ifreq));
        memcpy(ifr.ifr_name, ifname.data(), ifname.size());

        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(struct sockaddr_in));
        saddr.sin_family = AF_INET;
        saddr.sin_port = 0;

        Y_ENSURE(inet_pton(AF_INET, ip.c_str(), &saddr.sin_addr) == 1, "Invalid IPv4 address: " << ip);
        memcpy(&ifr.ifr_addr, &saddr, sizeof(struct sockaddr));
        IoctlOrThrow(socketFile.GetHandle(), SIOCSIFADDR, &ifr, "Cannot set network namespace interface address");

        Y_ENSURE(inet_pton(AF_INET, netmask.c_str(), &saddr.sin_addr) == 1, "Invalid IPv4 netmask: " << netmask);
        memcpy(&ifr.ifr_netmask, &saddr, sizeof(struct sockaddr));
        IoctlOrThrow(socketFile.GetHandle(), SIOCSIFNETMASK, &ifr, "Cannot set network namespace interface netmask");

        IoctlOrThrow(socketFile.GetHandle(), SIOCGIFFLAGS, &ifr, "Cannot read network namespace interface flags");
        ifr.ifr_flags |= IFF_UP;
        IoctlOrThrow(socketFile.GetHandle(), SIOCSIFFLAGS, &ifr, "Cannot bring network namespace interface up");
    }
#endif
} // namespace NNetNs

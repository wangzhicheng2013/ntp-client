#pragma once
#include <stdio.h> 
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>  
#include <sys/stat.h> 
#include <sys/ioctl.h> 
#include <sys/socket.h>
#include <sys/wait.h> 
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>

#include "single_instance.hpp"
struct ntp_time {
	unsigned int coarse = 0;
	unsigned int fine = 0;
};
class ntp_client {
public:
    bool get_ntp_time(const char *ntp_server, std::string &time_str, const char *format = "%Y-%m-%d %T") {
        struct timeval time_set = { 0 };
        struct hostent *host = gethostbyname(ntp_server);
        if (!host) {
            return false;
        }
        if (get_ntp_time(host, &time_set) < 0) {
            return false;
        }
        char buf[64] = "";
        struct tm *ptm = localtime(&time_set.tv_sec);
        strftime(buf, sizeof(buf), format, ptm);
        time_str = buf;
    }
    // return -1 if ntp server communication exception
    // return -2 if shell command execute failed
    int set_ntp_time(const char *ntp_server) {
        std::string time_str;
        if (false == get_ntp_time(ntp_server, time_str)) {
            return -1;
        }
        std::string shell_cmd = "date -s '";
        shell_cmd += time_str;
        shell_cmd += "'";
        if (system(shell_cmd.c_str())) {
            return -2;
        }
        return 0;
    }
private:
    inline long ntp_frac(long x) {
        return 4294 * (x) + ((1981 * (x))>>11);
    }
    inline long usec(long x) {
        return ((x) >> 12) - 759 * ((((x) >> 10) + 32768) >> 16);
    }
    void send_ntp_packet(int fd) {
        unsigned int data[12] = { 0 };
        struct timeval now = { 0 };
        // build ntp header
        data[0] = htonl((LI << 30) | (VN << 27) | (MODE << 24) | (STRATUM << 16) | (POLL << 8) | (PREC & 0xff));
        data[1] = htonl(1 << 16);
        data[2] = htonl(1 << 16);
        // build transmission timestamp
        gettimeofday(&now, nullptr);
        data[10] = htonl(now.tv_sec + JAN_1970); 
        data[11] = htonl(ntp_frac(now.tv_usec));
        send(fd, data, sizeof(data), 0);
    }
    inline void get_ntp_server_return_time(unsigned int *data, struct timeval *ptimeval) {
        struct ntp_time trantime;
        trantime.coarse = ntohl(((unsigned int *)data)[10]);
        trantime.fine = ntohl(((unsigned int *)data)[11]);
        ptimeval->tv_sec = trantime.coarse - JAN_1970;
        ptimeval->tv_usec = usec(trantime.fine);
    }
    int get_ntp_time(struct hostent* phost, struct timeval *ptimeval) {
        if ((nullptr == phost) || (nullptr == ptimeval)) {
            return -1;
        }
        int sockfd = -1;
        int recv_len = 0;
        int count = 0;
        unsigned int buf[12] = { 0 };

        struct sockaddr_in addr_src = { 0 };
        struct sockaddr_in addr_dst = { 0 };
        struct timeval timeout = { 0 };
        fd_set fds = { 0 };
        int addr_len = sizeof(struct sockaddr_in);
        addr_src.sin_family = AF_INET;
        addr_src.sin_addr.s_addr = htonl(INADDR_ANY);
        addr_src.sin_port = htons(0);

        addr_dst.sin_family = AF_INET;
        memcpy(&(addr_dst.sin_addr.s_addr), phost->h_addr_list[0], 4);
        addr_dst.sin_port = htons(123);         //ntp default port:123
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0 ) {		
            return -1;
        }
        int ret = bind(sockfd, (struct sockaddr*)&addr_src, addr_len);
        if (ret < 0) {		
            close(sockfd);
            return -1;
        }
        ret = connect(sockfd, (struct sockaddr*)&addr_dst, addr_len);
        if (ret < 0) {		
            close(sockfd);
            return -1;
        }
        send_ntp_packet(sockfd);
        const int LOOP = 50;
        while (count < LOOP) {
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
            ret = select(sockfd + 1, &fds, nullptr, nullptr, &timeout);
            if (0 == ret) {
                count++;
                send_ntp_packet(sockfd);
                usleep(100000);
                continue;
            }
            if (FD_ISSET(sockfd, &fds)) {
                recv_len = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr *)&addr_dst, (socklen_t*)&addr_len);
                if (recv_len < 0) {			
                    close(sockfd);
                    return -1;
                }
                else if(recv_len > 0) {
                    get_ntp_server_return_time(buf, ptimeval);
                    break;
                }
            }
            else {
                usleep(50000);
                count++;
            }
        }
        if (count >= LOOP) {
            close(sockfd);
            return -1;
        }
        close(sockfd);
        return 0;
    }

private:
    const long JAN_1970 = 0x83aa7e80;
    const int LI = 0;
    const int VN = 3;
    const int MODE = 3;
    const int STRATUM = 0;
    const int POLL = 4;
    const int PREC = -6;
};

#define  G_NTP_CLIENT single_instance<ntp_client>::instance()
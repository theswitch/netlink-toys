#include <linux/rtnetlink.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void rtnl_print_link(struct nlmsghdr *h) {
    struct ifinfomsg *iface;
    struct rtattr *attribute;
    int len;

    iface = NLMSG_DATA(h);
    len = h->nlmsg_len - NLMSG_LENGTH(sizeof(*iface));

    for (attribute = IFLA_RTA(iface); RTA_OK(attribute, len);
            attribute = RTA_NEXT(attribute, len)) {
        switch (attribute->rta_type) {
        case IFLA_IFNAME:
            printf("interface %d : %s\n", iface->ifi_index,
                    (char *) RTA_DATA(attribute));
            break;
        }
    }
}

int main(int argc, char **argv) {
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    struct sockaddr_nl local;
    pid_t pid = getpid();

    // bind local socket to process pid
    memset(&local, 0, sizeof(local));
    local.nl_family = AF_NETLINK;
    local.nl_pid = pid;
    local.nl_groups = 0;

    if (bind(sock, (struct sockaddr *) &local, sizeof(local)) < 0) {
        perror("bind");
        return 1;
    }

    struct nl_req_s {
        struct nlmsghdr hdr;
        struct rtgenmsg gen;
    };

    struct sockaddr_nl kernel;
    struct msghdr rtnl_msg;
    struct iovec io;
    struct nl_req_s req;

    memset(&rtnl_msg, 0, sizeof(rtnl_msg));
    memset(&kernel, 0, sizeof(kernel));
    memset(&req, 0, sizeof(req));

    kernel.nl_family = AF_NETLINK;

    req.hdr.nlmsg_len   = NLMSG_LENGTH(sizeof(struct rtgenmsg));
    req.hdr.nlmsg_type  = RTM_GETLINK;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.hdr.nlmsg_seq   = 1;
    req.hdr.nlmsg_pid   = pid;
    req.gen.rtgen_family = AF_PACKET;

    io.iov_base = &req;
    io.iov_len = req.hdr.nlmsg_len;
    rtnl_msg.msg_iov = &io;
    rtnl_msg.msg_iovlen = 1;
    rtnl_msg.msg_name = &kernel;
    rtnl_msg.msg_namelen = sizeof(kernel);

    sendmsg(sock, (struct msghdr *) &rtnl_msg, 0);

    int end = 0;
    char reply[4096];

    while (!end) {
        int len;
        struct nlmsghdr *msg_ptr;
        struct msghdr rtnl_reply;
        struct iovec io_reply;

        memset(&io_reply, 0, sizeof(io_reply));
        memset(&rtnl_reply, 0, sizeof(rtnl_reply));

        io.iov_base = reply;
        io.iov_len = 4096;
        rtnl_reply.msg_iov = &io;
        rtnl_reply.msg_iovlen = 1;
        rtnl_reply.msg_name = &kernel;
        rtnl_reply.msg_namelen = sizeof(kernel);

        len = recvmsg(sock, &rtnl_reply, 0);

        if (len) {
            for (msg_ptr = (struct nlmsghdr *) reply; NLMSG_OK(msg_ptr, len);
                    msg_ptr = NLMSG_NEXT(msg_ptr, len)) {
                switch (msg_ptr->nlmsg_type) {
                case 3: // NLMSG_DONE
                    end++;
                    break;
                case 16: // RTM_NEWLINK
                    rtnl_print_link(msg_ptr);
                    break;
                default:
                    fprintf(stderr, "message type %d, length %d\n",
                            msg_ptr->nlmsg_type, msg_ptr->nlmsg_len);
                    break;
                }
            }
        }
    }

    return 0;
}

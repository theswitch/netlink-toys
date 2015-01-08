#include <netlink/route/addr.h>
#include <stdio.h>
#include <stdbool.h>

static int handle_link(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[IFLA_MAX + 1];
    static struct nla_policy link_policy[IFLA_MAX + 1] = {
        [IFLA_OPERSTATE] = { .type = NLA_U8 },
        [IFLA_IFNAME]    = { .type = NLA_STRING },
    };

    nlmsg_parse(nlmsg_hdr(msg), sizeof(struct ifinfomsg), tb, IFLA_MAX, link_policy);

    return NL_OK;
}

int main(int argc, char **argv) {
    struct nl_sock *sock;
    struct nl_cb *cb;

    // allocate socket
    sock = nl_socket_alloc();
    cb = nl_cb_alloc(NL_CB_DEBUG);

    // connect to netlink_route
    if (nl_connect(sock, NETLINK_ROUTE) < 0) {
        fprintf(stderr, "nl_connect\n");
        return EXIT_FAILURE;
    }

    // join the link group
    if (nl_socket_add_memberships(sock, RTNLGRP_IPV4_IFADDR, NFNLGRP_NONE) < 0) {
        fprintf(stderr, "nl_socket_add_memberships\n");
        return EXIT_FAILURE;
    }

    // nl_cb_set(cb, NL_CB_MSG_IN, NL_CB_CUSTOM, handle_link, NULL);

    while (true)
        nl_recvmsgs(sock, cb);

    return EXIT_SUCCESS;
}

#include <linux/nl80211.h>
#include <netlink/attr.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/route/link.h>
#include <stdio.h>

const char *family = "nl80211";
const char *interface = "wlp3s0b1";

int handle_scan(struct nl_msg *nmsg, void *arg) {
    char *bssid = arg;
    // pull generic header out of the message
    struct genlmsghdr *ghdr = nlmsg_data(nlmsg_hdr(nmsg));

    // attribute data
    struct nlattr *tb_msg[NL80211_ATTR_MAX];
    struct nlattr *tb_bss[NL80211_BSS_MAX];

    // parsing policy
    static struct nla_policy bss_policy[NL80211_BSS_MAX] = {
        [NL80211_BSS_FREQUENCY]     = { .type = NLA_U32 }, // freq in MHz
        [NL80211_BSS_BSSID]         = { },                 // bssid (6 octets)
    };


    // parse the message
    nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(ghdr, 0),
            genlmsg_attrlen(ghdr, 0), NULL);

    if (!tb_msg[NL80211_ATTR_BSS]) {
        printf("bss info missing\n");
        return NL_SKIP;
    }

    nla_parse_nested(tb_bss, NL80211_BSS_MAX, tb_msg[NL80211_ATTR_BSS], bss_policy);

    memcpy(bssid, nla_data(tb_bss[NL80211_BSS_BSSID]), 6);

    printf("bssid: ");
    for (int i = 0; i < 6; i++)
        printf("%s%02x", i == 0 ? "" : ":", bssid[i]);
    printf("\n");

    printf("frequency: %d MHz\n", nla_get_u32(tb_bss[NL80211_BSS_FREQUENCY]));

    return NL_OK;
}

int handle_station(struct nl_msg *nmsg, void *arg) {
    struct genlmsghdr *ghdr = nlmsg_data(nlmsg_hdr(nmsg));
    struct nlattr *tb_msg[NL80211_ATTR_MAX];
    struct nlattr *tb_sta[NL80211_STA_INFO_MAX];
    struct nlattr *tb_txr[NL80211_RATE_INFO_MAX];

    // station info policy
    static struct nla_policy sta_policy[NL80211_STA_INFO_MAX] = {
        [NL80211_STA_INFO_RX_BYTES64] = { .type = NLA_U64 },
        [NL80211_STA_INFO_TX_BYTES64] = { .type = NLA_U64 },
        [NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
        [NL80211_STA_INFO_SIGNAL_AVG] = { .type = NLA_U8 },
    };

    // tx bitrate policy
    static struct nla_policy txr_policy[NL80211_RATE_INFO_MAX] = {
        [NL80211_RATE_INFO_BITRATE] = { .type = NLA_U16 },
    };

    nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(ghdr, 0),
            genlmsg_attrlen(ghdr, 0), NULL);

    if (!tb_msg[NL80211_ATTR_STA_INFO]) {
        printf("sta info missing\n");
        return NL_SKIP;
    }

    nla_parse_nested(tb_sta, NL80211_STA_INFO_MAX, tb_msg[NL80211_ATTR_STA_INFO], sta_policy);

    printf("rx bytes: %ld\n", nla_get_u64(tb_sta[NL80211_STA_INFO_RX_BYTES64]));
    printf("tx bytes: %ld\n", nla_get_u64(tb_sta[NL80211_STA_INFO_TX_BYTES64]));
    printf("av. sig: %d dBm\n", nla_get_u8(tb_sta[NL80211_STA_INFO_SIGNAL_AVG]));

    if (!tb_sta[NL80211_STA_INFO_TX_BITRATE]) {
        printf("rate info missing\n");
        return NL_SKIP;
    }

    nla_parse_nested(tb_txr, NL80211_RATE_INFO_MAX, tb_sta[NL80211_STA_INFO_TX_BITRATE], txr_policy);

    printf("tx bitrate: %.1f Mb/s\n", ((float) nla_get_u16(tb_txr[NL80211_RATE_INFO_BITRATE])) / 10);

    return NL_OK;
}

int main(int argc, char **argv) {
    int ret, code = EXIT_SUCCESS;
    struct nl_sock *nsock, *gsock;
    struct nl_msg *msg;
    struct rtnl_link *link;
    struct nl_cb *cb;
    int nlfamily, ifindex;
    char bssid[6];

    // allocate a socket and connect to the kernel
    gsock = nl_socket_alloc();
    if ((ret = genl_connect(gsock)) < 0) {
        fprintf(stderr, "genl_connect: %d\n", ret);
        exit(EXIT_FAILURE);
    }

    nsock = nl_socket_alloc();
    if ((ret = nl_connect(nsock, NETLINK_ROUTE)) < 0) {
        fprintf(stderr, "nl_connect: %d\n", ret);
        exit(EXIT_FAILURE);
    }

    if ((ret = rtnl_link_get_kernel(nsock, 0, interface, &link)) < 0) {
        if (ret == -NLE_OBJ_NOTFOUND)
            fprintf(stderr, "interface not found\n");
        else
            fprintf(stderr, "rtnl_link_get_kernel: %d\n", ret);

        goto err;
    }

    ifindex = rtnl_link_get_ifindex(link);

    rtnl_link_put(link);
    nl_socket_free(nsock);

    // resolve the numeric family identifier for NL80211
    if ((nlfamily = genl_ctrl_resolve(gsock, family)) < 0) {
        fprintf(stderr, "family %s is unknown\n", family);
        goto err;
    }

    // get scan results so we know the frequency and bssid
    msg = nlmsg_alloc();
    // put a header on the message
    genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, nlfamily,
            0, NLM_F_DUMP, NL80211_CMD_GET_SCAN, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);

    // send message
    if ((ret = nl_send_auto(gsock, msg)) < 0) {
        fprintf(stderr, "nl_send_auto: %d\n", ret);
        goto err;
    }
    nlmsg_free(msg);

    // set message receive callback
    cb = nl_cb_alloc(NL_CB_DEFAULT);
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, handle_scan, bssid);

    // receive messages from socket
    nl_recvmsgs(gsock, cb);

    // get station information
    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, nlfamily,
            0, 0, NL80211_CMD_GET_STATION, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);
    nla_put(msg, NL80211_ATTR_MAC, 6, bssid);
    if ((ret = nl_send_auto(gsock, msg)) < 0) {
        fprintf(stderr, "nl_send_auto: %d\n", ret);
        goto err;
    }
    nlmsg_free(msg);

    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, handle_station, NULL);
    nl_recvmsgs(gsock, cb);

    nl_cb_put(cb);
    nl_socket_free(gsock);

out:
    return code;

err:
    code = EXIT_FAILURE;
    goto out;
}

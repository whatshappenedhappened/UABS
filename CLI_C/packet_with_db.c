#include <stdio.h>
#include <pcap.h>
#include <arpa/inet.h>  // arpa/inet.h is included in pcap.h
#include <string.h>
#include <mariadb/mysql.h>
#include <stdlib.h>
#include <sys/socket.h> // guess this is already included at some header

// FOR OUTPUT OPTION //
// #define OUTPUT_MODE o_mode      // how the hell can i make this work
// #define OUTPUT_MODE_EX tmi
static int output_flag = 1;
static int output_select = 1;
static int frame = 1;

/* STATIC VARIABLES FOR MYSQL */
//MySQL
static MYSQL *connection, conn;
static MYSQL_RES *sql_result;
static MYSQL_ROW sql_row;       // must be freed before the program closed


/* Ethernet addresses are 6 bytes */
#define ETHER_ADDR_LEN	6       // ethernet address will always be 6B (0x00:0x00:0x00:0x00:0x00:0x00)
#define SIZE_ETHERNET   14      // ethernet header size, 6B of dst_MAC / 6B of src_MAC / 2B of Ether_type

/* STRUCTURE DEFINITION PART */
// Going to add got_packet and structures to use
typedef struct __dev_ip {
    unsigned char oct_first;
    unsigned char oct_second;
    unsigned char oct_third;
    unsigned char oct_fourth;
} DEV_IP;

typedef struct __http_payload {
    char http[320];             // http string
    char html[1024];             // html string
    unsigned int http_size;     // byte size of http string
    unsigned int html_size;     // byte size of html string
    unsigned int data_size;     // http_size + html size
} http_payload;

typedef struct __pseudo_header {
    u_int32_t s_ip;     // source ip address
    u_int32_t d_ip;     // destination ip address
    u_int8_t rsvd;      // 8 bits reserved, always 0
    u_int8_t prtc;      // protocol from ip header, tcp checksum will be 6
    u_int16_t tcp_len;  // length of tcp header + data
} pseudo_header;

/* Ethernet header */
typedef struct sniff_ethernet {
	u_char ether_dhost[ETHER_ADDR_LEN]; /* Destination host address */
	u_char ether_shost[ETHER_ADDR_LEN]; /* Source host address */
	u_short ether_type; /* IP? ARP? RARP? etc */
} layer2;

/* IP header */
typedef struct sniff_ip {
	u_char ip_vhl;		/* version << 4 | header length >> 2 */
	u_char ip_tos;		/* type of service */
	u_short ip_len;		/* total length */
	u_short ip_id;		/* identification */
	u_short ip_off;		/* fragment offset field */
#define IP_RF 0x8000		/* reserved fragment flag */
#define IP_DF 0x4000		/* don't fragment flag */
#define IP_MF 0x2000		/* more fragments flag */
#define IP_OFFMASK 0x1fff	/* mask for fragmenting bits */
	u_char ip_ttl;		/* time to live */
	u_char ip_p;		/* protocol */
	u_short ip_sum;		/* checksum */
	struct in_addr ip_src,ip_dst; /* source and dest address */
} layer3;
#define IP_HL(ip)		(((ip)->ip_vhl) & 0x0f) // & opt for left nibble, cuz ip header's maximum size is 20~60bytes
#define IP_V(ip)		(((ip)->ip_vhl) >> 4)   // bit opt 4 times to left for right nibble

/* TCP header */
typedef u_int tcp_seq;

typedef struct sniff_tcp {
	u_short th_sport;	/* source port */
	u_short th_dport;	/* destination port */
	tcp_seq th_seq;		/* sequence number */
	tcp_seq th_ack;		/* acknowledgement number */
	u_char th_offx2;	/* data offset, rsvd */
#define TH_OFF(th)	(((th)->th_offx2) >> 4) // there was a typo in original code. original : #define TH_OFF(th)	(((th)->th_offx2) > 4)
	u_char th_flags;
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PUSH 0x08
#define TH_ACK 0x10
#define TH_URG 0x20
#define TH_ECE 0x40
#define TH_CWR 0x80
#define TH_FLAGS (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
	u_short th_win;		/* window */
	u_short th_sum;		/* checksum */
	u_short th_urp;		/* urgent pointer */
}layer4;

///////////////////////////////

/* FUNCTION DECLARATION PART */
DEV_IP devnet(unsigned char *);
int devmask(bpf_u_int32);
void payloader(http_payload *);
void bongloader(http_payload *);

unsigned int get_checksum(unsigned short *cksum_packet, unsigned int cksum_len);

int sql_get_domain(char *url_name);       // returns 1 on matched url found, 0 on no url found

int sql_get_domain(char *url_name);       // returns 1 on matched url found, 0 on no url found

void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);

int sendraw(const u_char *packet, const struct pcap_pkthdr *header);

// Print function
void print_packet_hex(const unsigned char *packet, const struct pcap_pkthdr *header);
///////////////////

// void init_pcap();
// void init_mysql();

///////////////////////////////

int main(int argc, char *argv[]) {

    pcap_if_t *dev;
    char errbuf[PCAP_ERRBUF_SIZE];
    char *devc;
    //----
    bpf_u_int32 net, mask;
    //----
    pcap_t *handle;
    //----
    struct bpf_program fp;
    char *filter = "tcp port 80";
    //----
    const char *packet;
    struct pcap_pkthdr header;
    unsigned int p_loop_cnt = 5;

    // DEV_IP //
    u_char preFix;
    // MYSQL //
    connection = NULL;
    // OUTPUT MODE //
    // unsigned char o_mode = 1;
    // unsigned char tmi = 0;
    char * o_output = "Default";
    int temp;
    char func_str[1024];
    int func_str_len = 0;
    printf("\n");
    /* OUTPUT MODE */
    if (argc > 1 && argv[1]) {      // 실행시 입력값에 따라 상수 매크로의 값을 조절, -a는 모두 출력, 기본은 1
        if(strstr(argv[1], "-a") != NULL) {
            output_select = 1;
            output_flag = output_select;
            o_output = "A Friendly Neighbor";
        } else if (strstr(argv[1], "-h") != NULL) {
            output_select = 0;
            output_flag = output_select;
            o_output = "Hitman";
        }
    }
    printf("Callsign = \" %s \"\n", o_output);


    if (pcap_findalldevs(&dev, errbuf) == PCAP_ERROR) {
        printf("pcap_findalldevs() failed : %s\n", errbuf);
        return 1;
    } else {
        devc = dev->name;
        func_str_len = sprintf(func_str, "pcap_findalldevs() OK.\t\t\t[ Device ]\t%s\n", devc);
    }
    devc = "lo";
    
    if (pcap_lookupnet(devc, &net, &mask, errbuf) == PCAP_ERROR) {
        printf("pcap_lookupnet() failed : %s\n", errbuf);
        return 2;
    } else {
        struct in_addr ip;
        struct in_addr subnet;
        ip.s_addr = net;
        // subnet.s_addr = mask;
        preFix = devmask(mask);
        func_str_len += sprintf(func_str + func_str_len, "pcap_lookupnet() OK.\t\t\t[ IP ]\t\t%s/%d\n", inet_ntoa(ip), preFix);
    }

    handle = pcap_open_live(devc, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        printf("pcap_open_live() failed : %s", errbuf);
        return 3;
    } else { func_str_len += sprintf(func_str + func_str_len, "pcap_open_live() OK.\t\t\t[ Handler ]\t%p\n", handle);}
    ///// t-
    if (pcap_datalink(handle) != DLT_EN10MB) {
                fprintf(stderr, "Device %s doesn't provide Ethernet headers - not supported\n", dev);
                return(2);
        } else {
                printf("INFO: pcap_datalink OK\n");
        }
    /////

    if (pcap_compile(handle, &fp, filter, 0, net) == PCAP_ERROR) {
        printf("pcap_compile() failed : %s2\n",pcap_geterr(handle));
        return 4;
    } else { func_str_len += sprintf(func_str + func_str_len, "pcap_compile() OK.\t\t\t[ Filter ]\t%s\n", filter);}
    
    if (pcap_setfilter(handle, &fp) == PCAP_ERROR) {
        printf("pcap_setfilter() failed : %s\n", pcap_geterr(handle));
        return 5;
    } else { func_str_len += sprintf(func_str + func_str_len, "pcap_setfilter() OK.\n"); }

    printf("\n");

    // MYSQL CONNECTION
    if(mysql_init(&conn) == NULL) {
        fprintf(stderr, "mysql_init() failed.\n");
        return 6;
    } else { func_str_len += sprintf(func_str + func_str_len, "mysql_init() OK.\n"); }
    connection = mysql_real_connect(&conn, "localhost", "root", "root", "blocker", 3306, NULL, 0);
    if (connection == NULL) {
        fprintf(stderr, "mysql_real_connect() failed : %s\n", mysql_error(&conn));
        return 7;
    } else { func_str_len += sprintf(func_str + func_str_len, "mysql_real_connect() OK.\n"); }

    if (output_flag > 0)
        printf("%s", func_str);


    printf("\n\n");

    // packet = pcap_next(handle, &header);
    // printf("Packet just got jacked : [%d]bytes\n", header.len);
    puts("YOU'RE OUT got_packet()!!!!\n");
    // PCAP LOOP //
    pcap_loop(handle, 0, got_packet, NULL);
    // --------- //


    // PCAP CLOSE
    if (handle != NULL) {
        pcap_close(handle);
        handle = NULL;
    } else { puts("\nhandle is already closed\n"); }

    // MYSQL CLOSE
    if (&conn != NULL) {
        mysql_close(&conn);
    } else { puts("\nconn is already closed\n"); }
    if (sql_result != NULL) {
        mysql_free_result(sql_result);
        sql_result = NULL;
    } else { puts("\nsql_result is already closed\n"); }
    sql_result = NULL;

    return 0;
}

void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
    puts("YOU'RE IN got_packet()!!!!\n");
    layer2 *ethernet = (layer2 *)(packet);
    layer3 *ip = (layer3 *)(packet + SIZE_ETHERNET);
    u_int ip_size = 4 * IP_HL(ip);
    layer4 *th = (layer4 *)(packet + SIZE_ETHERNET + ip_size);
    u_int tcp_size = 4 * TH_OFF(th);
    const char* payload = (char *)(packet + SIZE_ETHERNET + ip_size + tcp_size);
    char* find_host = NULL;
    int url_size = 0;
    char url_name[512];
    char *url_from_db = NULL;
    int sql_get_flag = 0;

    /* ip output variables */
    char srcip[16], dstip[16];
    char *srcbf = inet_ntoa(ip->ip_src);
    strcpy(srcip, srcbf);
    char *dstbf = inet_ntoa(ip->ip_dst);
    strcpy(dstip, dstbf);

                                        // 23-02-16 Appended //
    // print_packet_hex(packet, header);

    // MYSQL
    // char *query_string = malloc(1048);      // must be freed before the function closed
    // memset(query_string, 0x00, 1048);
    puts("YOU'RE IN got_packet()!!!!\n");
    // FINDING HOST
    memset(url_name, 0x00, sizeof(url_name));
    find_host = strstr(payload, "Host:");
    if (find_host != NULL) {
        find_host += 6;
        url_size = strstr(find_host, "\x0d\x0a") - find_host;
        memcpy(url_name, find_host, url_size);
        printf("URL : %s\n", url_name);
        sql_get_flag = sql_get_domain(url_name);
        printf("sql_get_flag = %d\n\n\n", sql_get_flag);
    }

    if (sql_get_flag == 1) {
        sendraw(packet, header);
    }
    // sendraw(packet, header);

    // Query
    // sprintf(query_string, "SELECT ipv4, domain_name FROM tb_domains WHERE ipv4 = '%s'", dstip);
    // mysql_query(connection, query_string);
    // sql_result = mysql_store_result(connection);
    // sql_row = mysql_fetch_row(sql_result);

    // every data from packet will be stored as big-endian way except the type of char(1 byte)
    /* ethernet output */

    output_flag = output_select;
    
}
/*
plus all the cksum_packet data as 2bytes each.
if there is carry nibble(over 2bytes), cut the carry nibble and plus it to rest of 2bytes.
get the 1's complement of sum.
*/
unsigned int get_checksum(unsigned short *cksum_packet, unsigned int cksum_len) {
    unsigned int sum = 0;
    unsigned int carry_nibble = 0;
    printf("cksize = %d\n", cksum_len);
    while (cksum_len > 1) {
        // printf("sum = %d\tpacket = %d\t len = %d\n", sum, *cksum_packet, cksum_len);
        sum += *cksum_packet++;
        cksum_len -= 2;
    }

    if (cksum_len) {
        // printf("additional\nsum = %d\tpacket = %d\t len = %d\n", sum, *(unsigned char *)cksum_packet, cksum_len);
        sum += *cksum_packet++;
        cksum_len--;
    }

    carry_nibble = sum >> 16;
    sum += carry_nibble;
    carry_nibble = sum + ~sum;
    printf("sum = %x\nchecksum = %x\n", ~sum, carry_nibble);
    return ~sum;
}

int sendraw(const u_char *packet_ref, const struct pcap_pkthdr *header) {

    char packet_buffer[1600];
    layer2 *ethdr = (layer2 *)packet_ref;
    unsigned int vlan_size = 0;
    layer3 *iphdr;
    unsigned int iphdr_size = sizeof(layer3);
    layer4 *tcphdr;
    unsigned int tcphdr_size = sizeof(layer4);

    // for ack num
    u_int ref_payload_size; // for ref's payload size
    u_int ref_header_size; // ip_ref header size
    layer3 *ip_ref; // ip_ref
    layer4 *tcp_ref; // tcp_ref

    // for payload
    http_payload block_site;

    // for checksum
    pseudo_header *psd_hdr;
    char checksum_buffer[1024];

    // for raw socket
    int socket_raw;
    int socket_opt_value = 1;
    ssize_t sendto_result = 0;
    struct sockaddr_in target_addr;

    /* ETHERNET TYPE EXAMINATION */
    // ether_type take-up 2bytes. 0x8100 and 0x0800 represent vlan and normal ipv4 each.
    if (ethdr->ether_type == 0x81) {
        vlan_size = 4;
    } else if (ethdr->ether_type == 0x08) {
        vlan_size = 0;
    }

    ip_ref = (layer3 *)(packet_ref + vlan_size + SIZE_ETHERNET); // ip_ref
    ref_header_size = IP_HL(ip_ref) * 4; // ip_ref header size
    tcp_ref = (layer4 *)(packet_ref + vlan_size + SIZE_ETHERNET + ref_header_size); // tcp_ref
    ref_header_size += TH_OFF(tcp_ref) * 4; // tcp_ref header size
    ref_payload_size = ntohs(ip_ref->ip_len) - ref_header_size; // ref_payload size

    printf("+ack = %u\n", ref_payload_size);
    printf("offx2 = %x\nflags = %x\n", tcp_ref->th_offx2, tcp_ref->th_flags);

    /* TAMPERED PACKET CREATION */
    // clean-up all the memory bytes of packet_buffer[] and bind the two header variables to it.
    memset(packet_buffer, 0x00, 1600);
    iphdr = (layer3 *)(packet_buffer + vlan_size);
    tcphdr = (layer4 *)(packet_buffer + vlan_size + iphdr_size);

    // IP header
    // htonc, htons, htonl isn't necessary because the packet_ref is already loaded as Big-Endian.
    // and the headers we're creating here will be sent to the target network which is also Big-Endian.
    iphdr->ip_vhl = ip_ref->ip_vhl;
    iphdr->ip_tos = ip_ref->ip_tos;
    // iphdr->ip_len = ip_ref->ip_len;
    iphdr->ip_id = ip_ref->ip_id + htons(1); // should research about identification
    // iphdr->ip_off = ip_ref->ip_off; // 0 default
    iphdr->ip_ttl = 64;     // I just set this as 64, don't ask me why, anyway. :(
    iphdr->ip_p = 0x06;     // TCP == 0x06;
    iphdr->ip_src = ip_ref->ip_dst; // twist src ip and dst ip
    iphdr->ip_dst = ip_ref->ip_src; // because this will be sent to src ip from packet_ref
    iphdr->ip_sum = get_checksum((unsigned short *)iphdr, sizeof(layer3));
    
    // TCP header
    tcphdr->th_sport = tcp_ref->th_dport; // twist src port and dst port
    tcphdr->th_dport = tcp_ref->th_sport; // as the same reason as the ip hdr above
    tcphdr->th_seq = tcp_ref->th_ack; // seq is current total payload size sent to destination of one establish
    tcphdr->th_ack = tcp_ref->th_seq + htonl(ref_payload_size); // ack is current total payload size received from destiantion of one establish
    printf("temp = %u\nTam tcp_ack = %u\n", ntohl(tcp_ref->th_seq), ntohl(tcphdr->th_ack));
    tcphdr->th_offx2 = 0x50;
    tcphdr->th_flags = tcp_ref->th_flags + TH_FIN;
    tcphdr->th_win = tcp_ref->th_win;

    // HTTP + HTML payload bind
    // payloader(&block_site);
    bongloader(&block_site);
    memcpy((char *)packet_buffer + (vlan_size + iphdr_size + tcphdr_size), block_site.http, block_site.http_size);
    memcpy((char *)packet_buffer + (vlan_size + iphdr_size + tcphdr_size + block_site.http_size), block_site.html, block_site.html_size);

    printf("http_payload\n%s\n\n", (char *)packet_buffer + (vlan_size + iphdr_size + tcphdr_size));

    // Pseudo hedaer and buffer for checksum
    memset(checksum_buffer, 0x00, sizeof(checksum_buffer));

    psd_hdr = (pseudo_header *)checksum_buffer;
    psd_hdr->s_ip = iphdr->ip_src.s_addr;   // operand is already Big-Endian
    psd_hdr->d_ip = iphdr->ip_dst.s_addr;   // operand is already Big-Endian
    psd_hdr->rsvd = 0x00;                   // this should always be 0
    psd_hdr->prtc = IPPROTO_TCP;    // IPPTORO_TCP(which is 6) is defined in in.h
    psd_hdr->tcp_len = htons(tcphdr_size + block_site.data_size);   // host to network. lengh of header + data
    
    memcpy((char *)checksum_buffer + sizeof(pseudo_header), tcphdr, tcphdr_size);
    memcpy((char *)checksum_buffer + sizeof(pseudo_header) + tcphdr_size, block_site.http, block_site.http_size);
    memcpy((char *)checksum_buffer + sizeof(pseudo_header) + tcphdr_size + block_site.http_size, block_site.html, block_site.html_size);
    
    // is checksum should be htons?
    tcphdr->th_sum = get_checksum((unsigned short *)checksum_buffer, sizeof(pseudo_header) + tcphdr_size + block_site.data_size);
    // printf("psd = %x\n", psd_hdr->s_ip);
    // printf("short* = %02x:%p + %02x:%p = %02x\n", checksum_buffer[0], &checksum_buffer[0], (unsigned char)*(checksum_buffer+1), (unsigned char *)(checksum_buffer+1), (unsigned short)*(unsigned short *)checksum_buffer);
    iphdr->ip_len = htons(iphdr_size + vlan_size + tcphdr_size + block_site.data_size);
    printf("t_len = %d\n", ntohs(iphdr->ip_len));
    // Socket creation
    /*
    1st parameter:
    AF_INET is an Address Family for ipv4 protocol, PF_INET is a Protocol Family.
    can also use PF_PACKET, then you have to create L2 header too.
    2nd parameter:
    SOCK_STREAM is a type for connection-based streams which is TCP.
    SOCK_RAW designates the socket as a raw data socket,
    which it makes the programmer can control the header datas.
    when the type is SOCK_RAW, the socket regards all the header datas as user datas.
    Only root account can create RAW socket because the TCP/IP stack provided from kernel,
    which only root account can access to them.
    3rd parameter:
    IPPROTO_RAW : raw protocol, which is user have to customise ip header too.
    */
    puts("1\n");
    target_addr.sin_family = AF_INET;
    puts("2\n");
    target_addr.sin_port = tcphdr->th_dport;
    puts("3\n");
    target_addr.sin_addr = iphdr->ip_dst;
    puts("4\n");
    memset(target_addr.sin_zero, 0x00, sizeof(target_addr.sin_zero));
    socket_raw = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    /* IP_HDRINCL's value is 1(TRUE(or enable), socket_opt_value) means user is going to create an ip header,
    so notice to Ubuntu kernel to not modify ip header*/
    setsockopt(socket_raw, IPPROTO_IP, IP_HDRINCL, &socket_opt_value, sizeof(socket_opt_value));
    // sendto(); // 공부하기    // use iphdr->ip_len
    sendto_result = sendto(socket_raw, packet_buffer, ntohs(iphdr->ip_len), 0x00, (struct sockaddr *)&target_addr, sizeof(target_addr));
    if (sendto_result != ntohs(iphdr->ip_len)) {
        puts("sendto() failed.\n");
    } else if (sendto_result == ntohs(iphdr->ip_len)) puts("sendto() OK.\n\n");
    
    return 0;
}

void print_packet_hex(const unsigned char *packet, const struct pcap_pkthdr *header) {

    //length > 패킷 길이 전체.
    int length = header->len;
    //cnt > 갯수 세주는 변수.
    int cnt = 0;
    int i;
    int tab_cnt = 5;
    int space_cnt = 16;

    for (i = 0; i < tab_cnt; i ++) printf("\t");

    for (i = 0; i < length; length--) {

        printf("%02x ", *packet++);

        if ((++cnt % space_cnt) == 0) {  
            printf("\n");
            if (*(packet + space_cnt)) printf("\t\t\t\t\t");
        }
    }
}

int sql_get_domain(char * url_name) {
    char query_string[768];      // must be freed before the function closed
    memset(query_string, 0x00, 768);

    sprintf(query_string, "SELECT domain_name FROM tb_domains WHERE domain_name = '%s' LIMIT 1;", url_name);

    sql_row = NULL;

    if (mysql_query(connection, query_string) == 0) {
        puts("OK : SQL sent to DB server.");
    } else {
        return 0;
    }

    if ((sql_result = mysql_store_result(connection)) != NULL) {
       puts("OK : SQL result stored.");
    } else {
        return 0;
    }

    if ((sql_row = mysql_fetch_row(sql_result)) != NULL) {      // 기존 함수와 달리 결과가 없으면 NULL을 반환하는 mysql_fetch_row()의 성질을 이용하여 url이 db에 있는지 없는지를 비교
        
        puts("\n[Found a matched url from DB server]\n");
        
        return 1;
    } else {
        
        puts("[No matched url from DB server]\n");
        
        return 0;
    }
    return 0;
}

//////////////////// PACKET OUTPUT /////////////////////
// void print_packet_linear(const u_char *packet, const struct pcap_pkthdr *header) {
//     printf("[Ethernet]\n");
//     printf("dstMac= %02x:%02x:%02x:%02x:%02x:%02x\n", ethernet->ether_dhost[0],
//                         (*ethernet).ether_dhost[1],
//                         ethernet->ether_dhost[2],
//                         ethernet->ether_dhost[3],
//                         ethernet->ether_dhost[4],
//                         ethernet->ether_dhost[5]
//                         );
//     printf("srcMac= %02x:%02x:%02x:%02x:%02x:%02x\n", ethernet->ether_shost[0],
//                         (*ethernet).ether_shost[1],
//                         ethernet->ether_shost[2],
//                         ethernet->ether_shost[3],
//                         ethernet->ether_shost[4],
//                         ethernet->ether_shost[5]
//                         );
//     printf("ethType= %x\n", ethernet->ether_type);
//     printf("\n");

//     printf("[IP]\n");
//     printf("srcIp= %s\n", srcip);
//     printf("dstIp= %s\n", dstip);
//     // printf("sType= %d\n", ip->ip_tos);
//     printf("totalLen= %hd bytes\n", ntohs(ip->ip_len));
//     printf("headerLen= %hd bytes\n", IP_HL(ip) * 4);
//     printf("TTL= %d\n", ip->ip_ttl);

//     printf("\n");


//     /* tcp output */
//     printf("[TCP]\n");
//     printf("srcPort= %d\n", ntohs(th->th_sport));
//     printf("dstPort= %d\n", ntohs(th->th_dport));
//     printf("seq= %u\n", ntohl(th->th_seq));
//     printf("ack= %u\n", ntohl(th->th_ack));
//     printf("headerLen= %d\n", TH_OFF(th) * 4);
//     // printf("%x\n");
//     printf("\n");
    

//     /* payload output */
    
//     printf("[PAYLOAD]\n");
//     printf("%s\n", payload);
//     printf("==================================\n\n");
//     printf("[SQL RESULT]\n");
//     if (sql_row != NULL) {
//         printf("Found IP : %s\t%s\n\n", sql_row[0], sql_row[1]);
//     } else printf("-- No IP Found --\n\n");
//     printf("==================================\n\n");
// }

// void print_packet_sendraw(const u_char *packet, const struct pcap_pkthdr *header) {}


//////////////////// CUSTOMIZED IP CONVERT FUNCTION ////////////////////
DEV_IP devnet(unsigned char *net) {
    // unsigned char *oct = (unsigned char *)net;
    DEV_IP dip;
    dip.oct_first = *net;
    dip.oct_second = *(net + 1);
    dip.oct_third = net[2];
    dip.oct_fourth = net[3];

    return dip;
}

int devmask(bpf_u_int32 mask) {
    int cnt = 0;

    while (mask != 0) {
        cnt++;
        mask /= 2;
    }
    return cnt;
}
////////////////////////////////////////////////////////////////////////

/*
payloader() take-up a pointer of http_payload type variable.
the function creates a structure for the http packet payload.
*/
void payloader(http_payload *payload_buffer) {
    payload_buffer->html_size = sprintf(payload_buffer->html,
        "<!DOCTYPE html>\r\n"
        "<html>\r\n"
            "<head>\r\n"
                "<title>UABS - Unauthorized Access Blocked</title>\r\n"
            "</head>\r\n"
            "<body>\r\n"
                "<h1>ACCORDING TO THE COMPANY RULES, THE SITE JUST GOT BLOCKED.</h1>\r\n"
            "</body>\r\n"
        "</html>\r\n");

    payload_buffer->http_size = sprintf(payload_buffer->http,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n\r\n", payload_buffer->html_size);
    
    payload_buffer->data_size = payload_buffer->http_size + payload_buffer->html_size;
}

void bongloader(http_payload *payload_buffer) {
    payload_buffer->html_size = sprintf(payload_buffer->html,
        "<!DOCTYPE html>\r\n"
        "<html>\r\n"
            "<head>\r\n"
                "<title>PPP - Packet Pirate Project</title>\r\n"
                "<style>\r\n"
                    "body { background-color : grey }\r\n"
                "</style>\r\n"
            "</head>\r\n"
            "<body>\r\n"
                "<center>\r\n"
                "<img src=\"https://cdn.discordapp.com/attachments/1065602473248686233/1070207120600997908/image.png\" height=\"300px\">\r\n"
                "<h1>\" HAHA! I GOT YOU! \"</h1>\r\n"
                "<p>You were excited thinking of accessing some <b>Hyung-Ak</b> page, don't you?</p>\r\n"
                "<p>Well, fortunetely <b>The Mighty Bong</b> is always watching you!</p>\r\n"
                "</center>\r\n"
            "</body>\r\n"
        "</html>\r\n");

    payload_buffer->http_size = sprintf(payload_buffer->http,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n\r\n", payload_buffer->html_size);
    
    payload_buffer->data_size = payload_buffer->http_size + payload_buffer->html_size;
}


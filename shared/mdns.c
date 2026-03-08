#include "mdns.h"
#include "../shared/network_util.h"

#include <stdio.h>
#include <string.h>

static inline uint16_t htons_portable(uint16_t hostshort)
{
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

static inline uint32_t htonl_portable(uint32_t hostlong)
{
    return ((hostlong & 0xFF) << 24) |
           (((hostlong >> 8) & 0xFF) << 16) |
           (((hostlong >> 16) & 0xFF) << 8) |
           ((hostlong >> 24) & 0xFF);
}

static size_t mdns_encode_name(const char *name, uint8_t *buffer, size_t buffer_size)
{
    if (!name || !buffer || buffer_size == 0)
        return 0;

    size_t pos = 0;
    const char *start = name;

    while (*name && pos < buffer_size - 1)
    {
        if (*name == '.')
        {
            size_t label_len = (size_t)(name - start);
            if (label_len == 0 || label_len > 63)
                return 0;
            if (pos + label_len + 1 >= buffer_size)
                return 0;

            buffer[pos++] = (uint8_t)label_len;
            memcpy(&buffer[pos], start, label_len);
            pos += label_len;
            start = name + 1;
        }
        name++;
    }

    size_t label_len = (size_t)(name - start);
    if (label_len > 63)
        return 0;
    if (pos + label_len + 2 >= buffer_size)
        return 0;

    buffer[pos++] = (uint8_t)label_len;
    memcpy(&buffer[pos], start, label_len);
    pos += label_len;
    buffer[pos++] = 0;

    return pos;
}

static int mdns_write_encoded_name(const char *name, uint8_t *buffer, size_t buffer_size, size_t *pos)
{
    size_t n;

    if (!name || !buffer || !pos || *pos >= buffer_size)
        return 0;

    n = mdns_encode_name(name, &buffer[*pos], buffer_size - *pos);
    if (n == 0)
        return 0;

    *pos += n;
    return 1;
}

static dns_rr_t *mdns_begin_rr(uint8_t *buffer, size_t buffer_size, size_t *pos,
                               uint16_t type, uint16_t rr_class, uint32_t ttl)
{
    dns_rr_t *rr;

    if (!buffer || !pos || *pos + sizeof(dns_rr_t) > buffer_size)
        return NULL;

    rr = (dns_rr_t *)&buffer[*pos];
    rr->type = htons_portable(type);
    rr->class = htons_portable(rr_class);
    rr->ttl = htonl_portable(ttl);
    rr->rdlength = 0;
    *pos += sizeof(dns_rr_t);
    return rr;
}

static int mdns_add_ptr_record(const mdns_config_t *cfg, const char *full_service_name,
                               uint8_t *buffer, size_t buffer_size, size_t *pos, uint32_t ttl)
{
    dns_rr_t *rr;
    size_t rdata_start;

    if (!mdns_write_encoded_name(cfg->service_type, buffer, buffer_size, pos))
        return 0;

    rr = mdns_begin_rr(buffer, buffer_size, pos, 12, 0x0001, ttl);
    if (!rr)
        return 0;

    rdata_start = *pos;
    if (!mdns_write_encoded_name(full_service_name, buffer, buffer_size, pos))
        return 0;

    rr->rdlength = htons_portable((uint16_t)(*pos - rdata_start));
    return 1;
}

static int mdns_add_srv_record(const mdns_config_t *cfg, const char *full_service_name,
                               uint8_t *buffer, size_t buffer_size, size_t *pos, uint32_t ttl)
{
    dns_rr_t *rr;
    dns_srv_rdata_t *srv;
    size_t rdata_start;

    if (!mdns_write_encoded_name(full_service_name, buffer, buffer_size, pos))
        return 0;

    rr = mdns_begin_rr(buffer, buffer_size, pos, 33, 0x8001, ttl);
    if (!rr)
        return 0;

    if (*pos + sizeof(dns_srv_rdata_t) > buffer_size)
        return 0;

    rdata_start = *pos;
    srv = (dns_srv_rdata_t *)&buffer[*pos];
    srv->priority = htons_portable(0);
    srv->weight = htons_portable(0);
    srv->port = htons_portable(cfg->port);
    *pos += sizeof(dns_srv_rdata_t);

    if (!mdns_write_encoded_name(cfg->hostname, buffer, buffer_size, pos))
        return 0;

    rr->rdlength = htons_portable((uint16_t)(*pos - rdata_start));
    return 1;
}

static int mdns_add_a_record(const mdns_config_t *cfg, uint8_t *buffer, size_t buffer_size, size_t *pos, uint32_t ttl)
{
    dns_rr_t *rr;
    dns_a_rdata_t *a;
    uint32_t ipv4_nbo;

    if (!cfg->ipv4 || strlen(cfg->ipv4) == 0)
        return 1;

    // Convert string IP to network byte order
    if (net_str_to_ipv4(cfg->ipv4, &ipv4_nbo) != 0)
        return 0;

    if (!mdns_write_encoded_name(cfg->hostname, buffer, buffer_size, pos))
        return 0;

    rr = mdns_begin_rr(buffer, buffer_size, pos, 1, 0x8001, ttl);
    if (!rr)
        return 0;

    if (*pos + sizeof(dns_a_rdata_t) > buffer_size)
        return 0;

    a = (dns_a_rdata_t *)&buffer[*pos];
    a->addr = ipv4_nbo;
    *pos += sizeof(dns_a_rdata_t);
    rr->rdlength = htons_portable(sizeof(dns_a_rdata_t));
    return 1;
}

static int mdns_add_txt_record(const mdns_config_t *cfg, const char *full_service_name,
                               uint8_t *buffer, size_t buffer_size, size_t *pos, uint32_t ttl)
{
    dns_rr_t *rr;
    size_t rdata_start;
    uint16_t i;

    if (!cfg->txt_entries || cfg->txt_count == 0)
        return 1;

    if (!mdns_write_encoded_name(full_service_name, buffer, buffer_size, pos))
        return 0;

    rr = mdns_begin_rr(buffer, buffer_size, pos, 16, 0x8001, ttl);
    if (!rr)
        return 0;

    rdata_start = *pos;
    for (i = 0; i < cfg->txt_count; i++)
    {
        size_t n = strlen(cfg->txt_entries[i]);
        if (n > 255)
            n = 255;

        if (*pos + 1 + n > buffer_size)
            return 0;

        buffer[(*pos)++] = (uint8_t)n;
        memcpy(&buffer[*pos], cfg->txt_entries[i], n);
        *pos += n;
    }

    rr->rdlength = htons_portable((uint16_t)(*pos - rdata_start));
    return 1;
}

static size_t mdns_build_packet(const mdns_instance_t *instance, uint8_t *buffer, size_t buffer_size, uint32_t ttl)
{
    dns_header_t *hdr;
    const mdns_config_t *cfg;
    char full_service_name[256];
    size_t pos = 0;
    uint16_t answers = 0;

    if (!instance || !buffer || buffer_size < sizeof(dns_header_t))
        return 0;

    cfg = &instance->config;

    hdr = (dns_header_t *)&buffer[pos];
    memset(hdr, 0, sizeof(*hdr));
    hdr->flags = htons_portable(0x8400);
    pos += sizeof(*hdr);

    snprintf(full_service_name, sizeof(full_service_name), "%s.%s", cfg->instance_name, cfg->service_type);

    if (!mdns_add_ptr_record(cfg, full_service_name, buffer, buffer_size, &pos, ttl))
        return 0;
    answers++;

    if (!mdns_add_srv_record(cfg, full_service_name, buffer, buffer_size, &pos, ttl))
        return 0;
    answers++;

    if (cfg->ipv4 != 0)
    {
        if (!mdns_add_a_record(cfg, buffer, buffer_size, &pos, ttl))
            return 0;
        answers++;
    }

    if (cfg->txt_entries && cfg->txt_count > 0)
    {
        if (!mdns_add_txt_record(cfg, full_service_name, buffer, buffer_size, &pos, ttl))
            return 0;
        answers++;
    }

    hdr->ancount = htons_portable(answers);
    return pos;
}

static size_t mdns_decode_name(const uint8_t *buffer, size_t buffer_size, size_t offset,
                               char *name, size_t name_size)
{
    size_t pos = offset;
    size_t out = 0;

    if (!buffer || !name || name_size == 0 || offset >= buffer_size)
        return 0;

    while (pos < buffer_size)
    {
        uint8_t len = buffer[pos++];
        if (len == 0)
            break;

        if ((len & 0xC0) == 0xC0)
        {
            if (pos >= buffer_size)
                return 0;
            pos++;
            break;
        }

        if (pos + len > buffer_size)
            return 0;
        if (out + len + 1 >= name_size)
            return 0;

        memcpy(&name[out], &buffer[pos], len);
        out += len;
        name[out++] = '.';
        pos += len;
    }

    if (out == 0)
        name[0] = '\0';
    else
        name[out - 1] = '\0';

    return pos;
}

static int mdns_name_matches(const mdns_config_t *cfg, const char *qname)
{
    char full_service_name[256];

    if (!cfg || !qname)
        return 0;

    snprintf(full_service_name, sizeof(full_service_name), "%s.%s", cfg->instance_name, cfg->service_type);

#ifdef _WIN32
#define mdns_strcasecmp _stricmp
#else
#define mdns_strcasecmp strcasecmp
#endif

    if (mdns_strcasecmp(qname, cfg->service_type) == 0)
        return 1;
    if (mdns_strcasecmp(qname, full_service_name) == 0)
        return 1;
    if (mdns_strcasecmp(qname, cfg->hostname) == 0)
        return 1;

    return 0;
}

static int mdns_qtype_supported(uint16_t qtype)
{
    return (qtype == 1 ||  // A
            qtype == 12 || // PTR
            qtype == 16 || // TXT
            qtype == 33 || // SRV
            qtype == 255); // ANY
}

static mdns_error_t mdns_send_response(mdns_instance_t *instance, const char *dest_ip, uint16_t dest_port, uint32_t ttl)
{
    uint8_t packet[1500];
    size_t n;
    int sent;

    if (!instance || !instance->udp_socket || !dest_ip || dest_port == 0)
        return MDNS_ERR_INVALID_ARGS;

    n = mdns_build_packet(instance, packet, sizeof(packet), ttl);
    if (n == 0)
    {
        printf("[mdns] Packet build failed (buffer overflow or invalid)\n");
        return MDNS_ERR_BUFFER_OVERFLOW;
    }

    printf("[mdns] Sending %zu bytes to %s:%u (instance: %s, service: %s)\n",
           n, dest_ip, dest_port, instance->config.instance_name, instance->config.service_type);
    fflush(stdout);

    sent = udp_send(instance->udp_socket, packet, n, dest_ip, dest_port);
    if (sent < 0)
    {
        printf("[mdns] ERROR: udp_send failed (returned %d)\n", sent);
        return MDNS_ERR_SOCKET_ERROR;
    }

    printf("[mdns] Successfully sent %d bytes\n", sent);
    fflush(stdout);
    return MDNS_OK;
}

mdns_error_t mdns_create(mdns_instance_t *instance, const mdns_config_t *config, udp_socket_t *udp_socket)
{
    if (!instance || !config || !udp_socket)
        return MDNS_ERR_INVALID_ARGS;

    memset(instance, 0, sizeof(*instance));
    instance->config = *config;
    instance->udp_socket = udp_socket;
    return MDNS_OK;
}

mdns_error_t mdns_announce(mdns_instance_t *instance)
{
    return mdns_send_response(instance, MDNS_MCAST_ADDR, MDNS_PORT, 4500);
}

mdns_error_t mdns_goodbye(mdns_instance_t *instance)
{
    return mdns_send_response(instance, MDNS_MCAST_ADDR, MDNS_PORT, 0);
}

static mdns_error_t mdns_handle_packet_internal(mdns_instance_t *instance, const uint8_t *data, size_t len,
                                                const char *src_ip, uint16_t src_port)
{
    uint16_t flags;
    uint16_t qdcount;
    size_t pos;
    uint16_t i;

    if (!instance || !data || len < sizeof(dns_header_t))
        return MDNS_ERR_INVALID_ARGS;

    flags = (uint16_t)((data[2] << 8) | data[3]);
    if ((flags & 0x8000) != 0)
    {
        return MDNS_OK;
    }

    qdcount = (uint16_t)((data[4] << 8) | data[5]);
    pos = sizeof(dns_header_t);

    for (i = 0; i < qdcount; i++)
    {
        char qname[256];
        size_t next = mdns_decode_name(data, len, pos, qname, sizeof(qname));
        uint16_t qtype;
        uint16_t qclass_raw;
        uint16_t qclass;
        int qu_preferred;

        if (next == 0 || next + 4 > len)
            return MDNS_ERR_INVALID_DATA;

        qtype = (uint16_t)((data[next] << 8) | data[next + 1]);
        qclass_raw = (uint16_t)((data[next + 2] << 8) | data[next + 3]);
        qclass = (uint16_t)(qclass_raw & 0x7FFF);
        qu_preferred = (qclass_raw & 0x8000) ? 1 : 0;
        pos = next + 4;

        // Only respond for matching names, supported record types, and class IN/ANY.
        if (!mdns_name_matches(&instance->config, qname))
            continue;
        if (!mdns_qtype_supported(qtype))
            continue;
        if (!(qclass == 1 || qclass == 255))
            continue;

        if (qu_preferred && src_ip && src_ip[0] != '\0' && src_port != 0)
            return mdns_send_response(instance, src_ip, src_port, 4500);

        return mdns_announce(instance);
    }

    return MDNS_OK;
}

mdns_error_t mdns_handle_packet(mdns_instance_t *instance, const uint8_t *data, size_t len)
{
    return mdns_handle_packet_internal(instance, data, len, NULL, 0);
}

void mdns_run(mdns_instance_t *instance, int (*stop_fn)(void))
{
    uint8_t buffer[1500];
    char src_ip[64];
    uint16_t src_port = 0;

    if (!instance || !instance->udp_socket)
    {
        return;
    }

    // stop func is optional - if provided, it will be called periodically to check if we should exit the loop
    while (!stop_fn || !stop_fn())
    {
        int len = udp_receive(instance->udp_socket, buffer, sizeof(buffer), src_ip, &src_port, 1000);
        if (len > 0)
        {
            mdns_handle_packet_internal(instance, buffer, (size_t)len, src_ip, src_port);
        }
    }
}

void mdns_run_multiple(mdns_instance_t **instances, size_t count, int (*stop_fn)(void))
{
    uint8_t buffer[1500];
    char src_ip[64];
    uint16_t src_port = 0;
    size_t i;

    if (!instances || count == 0)
    {
        return;
    }

    // All instances share the same UDP socket; receive once per loop iteration
    udp_socket_t *shared_socket = instances[0]->udp_socket;

    while (!stop_fn || !stop_fn())
    {
        int len = udp_receive(shared_socket, buffer, sizeof(buffer), src_ip, &src_port, 100);
        if (len > 0)
        {
            // Dispatch the same packet to all instances
            for (i = 0; i < count; i++)
            {
                mdns_instance_t *instance = instances[i];
                if (instance && instance->udp_socket)
                {
                    mdns_handle_packet_internal(instance, buffer, (size_t)len, src_ip, src_port);
                }
            }
        }
    }
}
#include "mdns.h"
#include "network_util.h"

#include <stdio.h>
#include <string.h>

#define DNS_HEADER_SIZE 12
#define DNS_RR_SIZE 10

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static int write_name(const char *name, uint8_t *buffer, size_t capacity, size_t *position)
{
    size_t start = *position;
    if (!name || !name[0])
        return 0;
    while (*name)
    {
        const char *end = strchr(name, '.');
        size_t length = end ? (size_t)(end - name) : strlen(name);
        if (length == 0 || length > 63 || *position + 1 + length >= capacity)
            return 0;
        buffer[(*position)++] = (uint8_t)length;
        memcpy(buffer + *position, name, length);
        *position += length;
        name += length;
        if (*name == '.')
            name++;
    }
    if (*position >= capacity || *position - start + 1 > 255)
        return 0;
    buffer[(*position)++] = 0;
    return 1;
}

static uint8_t *begin_record(uint8_t *buffer, size_t capacity, size_t *position,
                             uint16_t type, uint16_t record_class, uint32_t ttl)
{
    uint8_t *record;
    if (*position + DNS_RR_SIZE > capacity)
        return NULL;
    record = buffer + *position;
    write_u16(record, type);
    write_u16(record + 2, record_class);
    write_u32(record + 4, ttl);
    write_u16(record + 8, 0);
    *position += DNS_RR_SIZE;
    return record;
}

static size_t build_packet(const mdns_instance_t *instance, uint8_t *buffer,
                           size_t capacity, uint32_t ttl)
{
    const mdns_config_t *config = &instance->config;
    char full_name[256];
    size_t position = DNS_HEADER_SIZE, start;
    uint8_t *record;
    uint16_t answers = 2;
    uint16_t i;
    int name_length;
    if (capacity < DNS_HEADER_SIZE)
        return 0;
    memset(buffer, 0, DNS_HEADER_SIZE);
    write_u16(buffer + 2, 0x8400);
    name_length = snprintf(full_name, sizeof(full_name), "%s.%s", config->instance_name, config->service_type);
    if (name_length < 0 || (size_t)name_length >= sizeof(full_name))
        return 0;

    if (!write_name(config->service_type, buffer, capacity, &position) ||
        !(record = begin_record(buffer, capacity, &position, 12, 1, ttl)))
        return 0;
    start = position;
    if (!write_name(full_name, buffer, capacity, &position))
        return 0;
    write_u16(record + 8, (uint16_t)(position - start));

    if (!write_name(full_name, buffer, capacity, &position) ||
        !(record = begin_record(buffer, capacity, &position, 33, 0x8001, ttl)) ||
        position + 6 > capacity)
        return 0;
    start = position;
    write_u16(buffer + position, 0);
    write_u16(buffer + position + 2, 0);
    write_u16(buffer + position + 4, config->port);
    position += 6;
    if (!write_name(config->hostname, buffer, capacity, &position))
        return 0;
    write_u16(record + 8, (uint16_t)(position - start));

    if (config->ipv4 && config->ipv4[0])
    {
        uint32_t address;
        if (net_str_to_ipv4(config->ipv4, &address) != 0 ||
            !write_name(config->hostname, buffer, capacity, &position) ||
            !(record = begin_record(buffer, capacity, &position, 1, 0x8001, ttl)) ||
            position + 4 > capacity)
            return 0;
        memcpy(buffer + position, &address, 4);
        position += 4;
        write_u16(record + 8, 4);
        answers++;
    }
    if (config->txt_count)
    {
        if (!write_name(full_name, buffer, capacity, &position) ||
            !(record = begin_record(buffer, capacity, &position, 16, 0x8001, ttl)))
            return 0;
        start = position;
        for (i = 0; i < config->txt_count; i++)
        {
            size_t length;
            if (!config->txt_entries[i])
                return 0;
            length = strlen(config->txt_entries[i]);
            if (length > 255 || position + 1 + length > capacity)
                return 0;
            buffer[position++] = (uint8_t)length;
            memcpy(buffer + position, config->txt_entries[i], length);
            position += length;
        }
        write_u16(record + 8, (uint16_t)(position - start));
        answers++;
    }
    write_u16(buffer + 6, answers);
    return position;
}

static size_t decode_name(const uint8_t *data, size_t length, size_t offset,
                          char *name, size_t capacity)
{
    size_t position = offset, next = 0, used = 0, steps;
    for (steps = 0; steps < length; steps++)
    {
        uint8_t label_length;
        if (position >= length)
            return 0;
        label_length = data[position++];
        if (label_length == 0)
        {
            if (used == 0)
                name[0] = '\0';
            else
                name[used - 1] = '\0';
            return next ? next : position;
        }
        if ((label_length & 0xc0) == 0xc0)
        {
            size_t target;
            if (position >= length)
                return 0;
            target = ((size_t)(label_length & 0x3f) << 8) | data[position++];
            if (!next)
                next = position;
            position = target;
            continue;
        }
        if ((label_length & 0xc0) || position + label_length > length ||
            used + label_length + 1 >= capacity)
            return 0;
        memcpy(name + used, data + position, label_length);
        used += label_length;
        name[used++] = '.';
        position += label_length;
    }
    return 0;
}

static int name_matches(const mdns_config_t *config, const char *name)
{
    char full_name[256];
    int length = snprintf(full_name, sizeof(full_name), "%s.%s", config->instance_name, config->service_type);
    if (length < 0 || (size_t)length >= sizeof(full_name))
        return 0;
    return net_ascii_casecmp(name, config->service_type) == 0 ||
           net_ascii_casecmp(name, full_name) == 0 ||
           net_ascii_casecmp(name, config->hostname) == 0;
}

static mdns_error_t send_response(mdns_instance_t *instance, const net_addr_t *destination, uint32_t ttl)
{
    uint8_t packet[1500];
    size_t length;
    int sent;
    if (!instance || !instance->socket || !destination)
        return MDNS_ERR_INVALID_ARGS;
    length = build_packet(instance, packet, sizeof(packet), ttl);
    if (!length)
        return MDNS_ERR_BUFFER_OVERFLOW;
    sent = net_udp_send(instance->socket, packet, length, destination);
    return sent == (int)length ? MDNS_OK : MDNS_ERR_SOCKET_ERROR;
}

mdns_error_t mdns_create(mdns_instance_t *instance, const mdns_config_t *config, net_socket_t *socket)
{
    if (!instance || !config || !socket || !config->service_type ||
        !config->instance_name || !config->hostname || (config->txt_count && !config->txt_entries))
        return MDNS_ERR_INVALID_ARGS;
    instance->config = *config;
    instance->socket = socket;
    return MDNS_OK;
}

mdns_error_t mdns_announce(mdns_instance_t *instance)
{
    const net_addr_t destination = {MDNS_MCAST_ADDR, MDNS_PORT};
    return send_response(instance, &destination, 60);
}

mdns_error_t mdns_goodbye(mdns_instance_t *instance)
{
    const net_addr_t destination = {MDNS_MCAST_ADDR, MDNS_PORT};
    return send_response(instance, &destination, 0);
}

mdns_error_t mdns_handle_packet_from(mdns_instance_t *instance, const uint8_t *data,
                                     size_t length, const net_addr_t *source)
{
    uint16_t questions, i;
    size_t position = DNS_HEADER_SIZE;
    if (!instance || !data || length < DNS_HEADER_SIZE)
        return MDNS_ERR_INVALID_ARGS;
    if (read_u16(data + 2) & 0x8000)
        return MDNS_OK;
    questions = read_u16(data + 4);
    for (i = 0; i < questions; i++)
    {
        char name[256];
        size_t next = decode_name(data, length, position, name, sizeof(name));
        uint16_t type, raw_class, record_class;
        if (!next || next + 4 > length)
            return MDNS_ERR_INVALID_DATA;
        type = read_u16(data + next);
        raw_class = read_u16(data + next + 2);
        record_class = (uint16_t)(raw_class & 0x7fff);
        position = next + 4;
        if (!name_matches(&instance->config, name) ||
            !(type == 1 || type == 12 || type == 16 || type == 33 || type == 255) ||
            !(record_class == 1 || record_class == 255))
            continue;
        if ((raw_class & 0x8000) && source && source->ip[0] && source->port)
            return send_response(instance, source, 4500);
        return mdns_announce(instance);
    }
    return MDNS_OK;
}

mdns_error_t mdns_handle_packet(mdns_instance_t *instance, const uint8_t *data, size_t length)
{
    return mdns_handle_packet_from(instance, data, length, NULL);
}

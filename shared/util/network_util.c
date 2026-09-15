#include "util/network_util.h"

#include <stdio.h>
#include <string.h>

int net_str_to_ipv4(const char *ip, uint32_t *address)
{
    uint8_t bytes[4];
    size_t i;
    if (!ip || !address)
        return -1;
    for (i = 0; i < 4; i++)
    {
        unsigned value = 0;
        unsigned digits = 0;
        while (*ip >= '0' && *ip <= '9')
        {
            value = value * 10 + (unsigned)(*ip++ - '0');
            if (++digits > 3 || value > 255)
                return -1;
        }
        if (digits == 0 || (i < 3 ? *ip != '.' : *ip != '\0'))
            return -1;
        bytes[i] = (uint8_t)value;
        if (i < 3)
            ip++;
    }
    memcpy(address, bytes, sizeof(bytes));
    return 0;
}

int net_ipv4_to_str(uint32_t address, char *text, size_t capacity)
{
    uint8_t bytes[4];
    int length;
    if (!text || capacity < 16)
        return -1;
    memcpy(bytes, &address, sizeof(bytes));
    length = snprintf(text, capacity, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    return length > 0 && (size_t)length < capacity ? 0 : -1;
}

/**
 * @brief ascii_lower.
 * @param c Parameter named c.
 * @return Function result.
 */
static unsigned char ascii_lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

int net_ascii_ncasecmp(const char *a, const char *b, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
    {
        unsigned char left = ascii_lower((unsigned char)a[i]);
        unsigned char right = ascii_lower((unsigned char)b[i]);
        if (left != right || left == 0)
            return (int)left - (int)right;
    }
    return 0;
}

int net_ascii_casecmp(const char *a, const char *b)
{
    for (;;)
    {
        unsigned char left = ascii_lower((unsigned char)*a++);
        unsigned char right = ascii_lower((unsigned char)*b++);
        if (left != right || left == 0)
            return (int)left - (int)right;
    }
}

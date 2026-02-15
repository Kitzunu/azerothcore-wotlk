/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IpAddressMatcher_h__
#define IpAddressMatcher_h__

#include "Define.h"
#include <string>
#include <vector>

namespace Acore::Net
{
    /**
     * @brief Matches IPv4 addresses against patterns with wildcard support
     *
     * Supports patterns like:
     * - Exact IP: "192.168.0.1"
     * - Wildcard octets: "192.168.0.%" (matches 192.168.0.0 to 192.168.0.254)
     * - Multiple patterns: "127.0.0.1,192.168.%,10.0.0.%"
     *
     * The '%' character represents any value from 0 to 254 in that octet.
     * An empty pattern list allows all IPs.
     */
    class AC_COMMON_API IpAddressMatcher
    {
    public:
        /**
         * @brief Construct a matcher with a pattern string
         * @param patterns Comma-separated list of IP patterns (e.g., "192.168.0.%,10.0.0.1")
         *                 Empty string means allow all IPs
         */
        explicit IpAddressMatcher(std::string const& patterns);

        /**
         * @brief Check if an IP address matches any of the configured patterns
         * @param ip IPv4 address string (e.g., "192.168.0.42")
         * @return true if the IP matches any pattern or if no patterns are configured
         */
        [[nodiscard]] bool Matches(std::string const& ip) const;

    private:
        struct IpPattern
        {
            uint8 octet[4];
            bool wildcard[4];
        };

        [[nodiscard]] bool MatchesPattern(uint8 const ipOctets[4], IpPattern const& pattern) const;
        [[nodiscard]] static bool ParseIp(std::string const& ip, uint8 octets[4]);
        [[nodiscard]] static bool ParsePattern(std::string const& pattern, IpPattern& out);

        std::vector<IpPattern> _patterns;
        bool _allowAll;
    };
}

#endif // IpAddressMatcher_h__

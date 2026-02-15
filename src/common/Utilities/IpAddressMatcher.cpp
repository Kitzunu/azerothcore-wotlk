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

#include "IpAddressMatcher.h"
#include "Tokenize.h"
#include <sstream>

namespace Acore::Net
{
    IpAddressMatcher::IpAddressMatcher(std::string const& patterns)
        : _allowAll(patterns.empty())
    {
        if (_allowAll)
            return;

        // Split by comma
        std::vector<std::string_view> patternList = Acore::Tokenize(patterns, ',', false);

        for (auto const& patternStr : patternList)
        {
            // Trim whitespace
            std::string pattern(patternStr);
            size_t start = pattern.find_first_not_of(" \t\r\n");
            size_t end = pattern.find_last_not_of(" \t\r\n");

            if (start == std::string::npos || end == std::string::npos)
                continue;

            pattern = pattern.substr(start, end - start + 1);

            if (pattern.empty())
                continue;

            IpPattern ipPattern;
            if (ParsePattern(pattern, ipPattern))
                _patterns.push_back(ipPattern);
        }

        // If we couldn't parse any valid patterns, treat as allow all
        if (_patterns.empty())
            _allowAll = true;
    }

    bool IpAddressMatcher::Matches(std::string const& ip) const
    {
        if (_allowAll)
            return true;

        uint8 ipOctets[4];
        if (!ParseIp(ip, ipOctets))
            return false;

        for (auto const& pattern : _patterns)
        {
            if (MatchesPattern(ipOctets, pattern))
                return true;
        }

        return false;
    }

    bool IpAddressMatcher::MatchesPattern(uint8 const ipOctets[4], IpPattern const& pattern) const
    {
        for (int i = 0; i < 4; ++i)
        {
            if (!pattern.wildcard[i] && ipOctets[i] != pattern.octet[i])
                return false;
        }
        return true;
    }

    bool IpAddressMatcher::ParseIp(std::string const& ip, uint8 octets[4])
    {
        std::istringstream stream(ip);
        std::string octetStr;
        int index = 0;

        while (std::getline(stream, octetStr, '.') && index < 4)
        {
            try
            {
                int value = std::stoi(octetStr);
                if (value < 0 || value > 255)
                    return false;
                octets[index++] = static_cast<uint8>(value);
            }
            catch (...)
            {
                return false;
            }
        }

        // Verify we got exactly 4 octets and stream is exhausted
        if (index != 4)
            return false;

        // Check if there are any trailing characters
        std::string remaining;
        std::getline(stream, remaining);
        return remaining.empty();
    }

    bool IpAddressMatcher::ParsePattern(std::string const& pattern, IpPattern& out)
    {
        std::istringstream stream(pattern);
        std::string octetStr;
        int index = 0;

        while (std::getline(stream, octetStr, '.') && index < 4)
        {
            if (octetStr == "%")
            {
                out.octet[index] = 0;
                out.wildcard[index] = true;
            }
            else
            {
                try
                {
                    int value = std::stoi(octetStr);
                    if (value < 0 || value > 255)
                        return false;
                    out.octet[index] = static_cast<uint8>(value);
                    out.wildcard[index] = false;
                }
                catch (...)
                {
                    return false;
                }
            }
            ++index;
        }

        // Verify we got exactly 4 octets and stream is exhausted
        if (index != 4)
            return false;

        // Check if there are any trailing characters
        std::string remaining;
        std::getline(stream, remaining);
        return remaining.empty();
    }
}

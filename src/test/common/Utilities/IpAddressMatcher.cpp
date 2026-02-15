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
#include "gtest/gtest.h"

using namespace Acore::Net;

TEST(IpAddressMatcher, EmptyPatternAllowsAll)
{
    IpAddressMatcher matcher("");

    EXPECT_TRUE(matcher.Matches("192.168.0.1"));
    EXPECT_TRUE(matcher.Matches("10.0.0.1"));
    EXPECT_TRUE(matcher.Matches("127.0.0.1"));
    EXPECT_TRUE(matcher.Matches("8.8.8.8"));
}

TEST(IpAddressMatcher, ExactMatch)
{
    IpAddressMatcher matcher("192.168.0.1");

    EXPECT_TRUE(matcher.Matches("192.168.0.1"));
    EXPECT_FALSE(matcher.Matches("192.168.0.2"));
    EXPECT_FALSE(matcher.Matches("192.168.1.1"));
    EXPECT_FALSE(matcher.Matches("10.0.0.1"));
}

TEST(IpAddressMatcher, SingleOctetWildcard)
{
    IpAddressMatcher matcher("192.168.0.%");

    EXPECT_TRUE(matcher.Matches("192.168.0.0"));
    EXPECT_TRUE(matcher.Matches("192.168.0.1"));
    EXPECT_TRUE(matcher.Matches("192.168.0.42"));
    EXPECT_TRUE(matcher.Matches("192.168.0.254"));
    EXPECT_TRUE(matcher.Matches("192.168.0.255"));
    EXPECT_FALSE(matcher.Matches("192.168.1.1"));
    EXPECT_FALSE(matcher.Matches("192.167.0.1"));
    EXPECT_FALSE(matcher.Matches("10.168.0.1"));
}

TEST(IpAddressMatcher, MultipleOctetWildcards)
{
    IpAddressMatcher matcher("192.168.%.%");

    EXPECT_TRUE(matcher.Matches("192.168.0.0"));
    EXPECT_TRUE(matcher.Matches("192.168.0.1"));
    EXPECT_TRUE(matcher.Matches("192.168.1.1"));
    EXPECT_TRUE(matcher.Matches("192.168.255.255"));
    EXPECT_FALSE(matcher.Matches("192.167.0.1"));
    EXPECT_FALSE(matcher.Matches("10.168.0.1"));
}

TEST(IpAddressMatcher, MultiplePatterns)
{
    IpAddressMatcher matcher("127.0.0.1,192.168.0.%,10.0.0.42");

    EXPECT_TRUE(matcher.Matches("127.0.0.1"));
    EXPECT_TRUE(matcher.Matches("192.168.0.1"));
    EXPECT_TRUE(matcher.Matches("192.168.0.254"));
    EXPECT_TRUE(matcher.Matches("10.0.0.42"));
    EXPECT_FALSE(matcher.Matches("127.0.0.2"));
    EXPECT_FALSE(matcher.Matches("192.168.1.1"));
    EXPECT_FALSE(matcher.Matches("10.0.0.43"));
}

TEST(IpAddressMatcher, PatternsWithSpaces)
{
    IpAddressMatcher matcher("127.0.0.1 , 192.168.0.% , 10.0.0.42");

    EXPECT_TRUE(matcher.Matches("127.0.0.1"));
    EXPECT_TRUE(matcher.Matches("192.168.0.1"));
    EXPECT_TRUE(matcher.Matches("10.0.0.42"));
}

TEST(IpAddressMatcher, InvalidIpReturnsFalse)
{
    IpAddressMatcher matcher("192.168.0.1");

    EXPECT_FALSE(matcher.Matches("192.168.0"));
    EXPECT_FALSE(matcher.Matches("192.168.0.1.1"));
    EXPECT_FALSE(matcher.Matches("192.168.0.256"));
    EXPECT_FALSE(matcher.Matches("not.an.ip.address"));
    EXPECT_FALSE(matcher.Matches(""));
}

TEST(IpAddressMatcher, AllWildcards)
{
    IpAddressMatcher matcher("%.%.%.%");

    EXPECT_TRUE(matcher.Matches("0.0.0.0"));
    EXPECT_TRUE(matcher.Matches("127.0.0.1"));
    EXPECT_TRUE(matcher.Matches("192.168.0.1"));
    EXPECT_TRUE(matcher.Matches("255.255.255.255"));
}

TEST(IpAddressMatcher, FirstOctetWildcard)
{
    IpAddressMatcher matcher("%.0.0.1");

    EXPECT_TRUE(matcher.Matches("0.0.0.1"));
    EXPECT_TRUE(matcher.Matches("127.0.0.1"));
    EXPECT_TRUE(matcher.Matches("192.0.0.1"));
    EXPECT_FALSE(matcher.Matches("192.0.0.2"));
    EXPECT_FALSE(matcher.Matches("192.0.1.1"));
}

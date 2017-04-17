//
// Created by root on 17-4-17.
//

#include <unordered_map>

#include "../../dpi/Sharding.h"
#include "http_parser.h"

#ifndef NOFF_HTTPPARSERTABLE_H
#define NOFF_HTTPPARSERTABLE_H

inline bool httpParseEqual(
        const http_parser &lhs,
        const http_parser &rhs)
{
    return lhs.data == rhs.data;
}

typedef std::unordered_map<
        Sharding::Tuple4,
        http_parser,
        Sharding,
        decltype(httpParseEqual)> HttpParserTable;

#endif //NOFF_HTTPPARSERTABLE_H

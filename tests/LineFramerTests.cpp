#include "Net/Detail/LineFramer.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const char* description)
{
    if (!condition) {
        throw std::runtime_error(description);
    }
}

void expect_line(serverengine::net::detail::LineFramer& framer, const std::string& expected)
{
    std::string line;
    check(framer.try_pop(line), "expected a complete line");
    check(line == expected, "wrong line content");
}

} // namespace

int main()
{
    try {
        using serverengine::net::detail::LineFramer;
        std::string line;
        LineFramer fragmented(5);
        check(fragmented.append({}), "empty read is harmless");
        check(fragmented.append("HE"), "accept first fragment");
        check(!fragmented.try_pop(line), "wait for newline");
        check(fragmented.append("LLO"), "allow exactly the limit");
        check(fragmented.append("\n"), "newline must not exceed limit");
        expect_line(fragmented, "HELLO");

        LineFramer combined(3);
        check(combined.append("A\nBC\nDEF\n"), "limit applies per line, not per read");
        expect_line(combined, "A");
        expect_line(combined, "BC");
        expect_line(combined, "DEF");
        check(!combined.try_pop(line), "all combined lines consumed");

        LineFramer crlf(4);
        check(crlf.append("\nAB\r"), "empty line and fragmented CRLF");
        expect_line(crlf, "");
        check(!crlf.try_pop(line), "CR alone is not a terminator");
        check(crlf.append("\nCD\nE"), "append after consuming prefix");
        expect_line(crlf, "AB");
        expect_line(crlf, "CD");
        check(!crlf.try_pop(line), "do not deliver unterminated suffix");

        LineFramer oversized(3);
        check(oversized.append("ABC"), "allow pending line at limit");
        check(!oversized.append("D"), "reject oversized partial line");
        check(!oversized.append("\n"), "failure stays terminal");
        check(!oversized.try_pop(line), "failed connection emits no data");
        LineFramer oversized_complete(3);
        check(!oversized_complete.append("ABCD\n"), "reject oversized complete line");
        LineFramer mixed_invalid(3);
        check(!mixed_invalid.append("A\nABCD"), "reject read containing an oversized suffix");
        check(!mixed_invalid.try_pop(line), "invalid read discards its valid prefix too");
        LineFramer binary(3);
        check(binary.append(std::string("A\0B\n", 4)), "preserve embedded null bytes");
        expect_line(binary, std::string("A\0B", 3));
        std::cout << "PASS LineFramer\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

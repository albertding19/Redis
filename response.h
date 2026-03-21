
#pragma once
#include <vector>
#include <cstdlib>

struct Response {
    uint32_t status = 0;
    std::vector<uint8_t> data;
};

void make_response(const Response &resp, std::vector<uint8_t> out);
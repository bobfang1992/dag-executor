#include <iostream>
#include <sstream>
#include <random>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;  // ordered for deterministic output

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t a = dis(gen);
    uint64_t b = dis(gen);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << ((a >> 32) & 0xFFFFFFFF) << "-";
    ss << std::setw(4) << ((a >> 16) & 0xFFFF) << "-";
    ss << std::setw(4) << ((a) & 0xFFFF) << "-";
    ss << std::setw(4) << ((b >> 48) & 0xFFFF) << "-";
    ss << std::setw(12) << (b & 0xFFFFFFFFFFFF);
    return ss.str();
}

int main() {
    // Read all stdin
    std::ostringstream input_buf;
    input_buf << std::cin.rdbuf();
    std::string input = input_buf.str();

    // Parse request
    json request;
    try {
        request = json::parse(input);
    } catch (const json::parse_error& e) {
        json error_response;
        error_response["error"] = "Invalid JSON input";
        error_response["detail"] = e.what();
        std::cout << error_response.dump() << std::endl;
        return 1;
    }

    // Build response
    json response;

    // request_id: echo if provided, generate otherwise
    if (request.contains("request_id") && request["request_id"].is_string()) {
        response["request_id"] = request["request_id"];
    } else {
        response["request_id"] = generate_uuid();
    }

    // engine_request_id: always generated
    response["engine_request_id"] = generate_uuid();

    // Generate 5 synthetic candidates
    json candidates = json::array();
    for (int i = 1; i <= 5; ++i) {
        json candidate;
        candidate["id"] = i;
        candidate["fields"] = json::object();
        candidates.push_back(candidate);
    }
    response["candidates"] = candidates;

    std::cout << response.dump() << std::endl;
    return 0;
}

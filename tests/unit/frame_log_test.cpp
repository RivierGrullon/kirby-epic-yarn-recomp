// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/app/frame_log.h"
#include "filesystem_fixture.h"
#include <fstream>
#include <iostream>

int main() {
    const auto root = TestDirectory("kirby-frame-log-");
    try {
        const auto path = root / std::filesystem::path(u8"測定.csv");
        {
            Kirby::FrameLog log(path);
            Kirby::FrameMetrics sample{};
            sample.sequence = 1;
            sample.viField = 2;
            sample.startNs = 4294967297ull;
            log.Write(sample);
        }
        std::ifstream input(path, std::ios::binary);
        const std::string original{std::istreambuf_iterator<char>(input), {}};
        input.close();
        if (!original.starts_with("sequence,vi_field,start_ns,") ||
            original.find("\n1,2,4294967297,") == std::string::npos)
            throw std::runtime_error("Frame log did not preserve CSV header and 64-bit metrics");
        bool rejected = false;
        try { Kirby::FrameLog duplicate(path); } catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Frame log overwrote an existing capture");
        input.open(path, std::ios::binary);
        const std::string after{std::istreambuf_iterator<char>(input), {}};
        input.close();
        if (after != original) throw std::runtime_error("Rejected frame log creation damaged existing data");
        std::filesystem::remove_all(root);
        std::cout << "3 frame log checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}

#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "optional.hpp"

#include "garglk.h"

Bleeps::Bleeps() {
    update(1, 0.1, 1175);
    update(2, 0.1, 440);
}

// 22050Hz, unsigned 8 bits per sample, mono
void Bleeps::update(int number, double duration, int frequency) {
    if (number != 1 && number != 2)
        return;

    std::uint32_t samplerate = 22050;

    std::vector<std::uint8_t> data;
    data.reserve(duration * samplerate);
    for (std::size_t i = 0; i < duration * samplerate; i++)
    {
        auto point = 1 + std::sin(frequency * 2 * M_PI * i / static_cast<double>(samplerate));
        data.push_back(127 * point);
    }

    if (data.empty())
    {
        m_bleeps.at(number) = nonstd::nullopt;
        return;
    }

    // Turn the data into a WAV file
    std::uint32_t size = data.size();
    if (size % 2 == 1)
        size++;
    std::uint16_t pcm = 1;
    std::uint16_t channels = 1;
    std::uint16_t bytes = 1;
    std::uint32_t byterate = samplerate * channels * bytes;
    std::uint16_t blockalign = channels * bytes;
    std::uint16_t bits_per_sample = 8 * bytes;

#define n16(n) \
    static_cast<std::uint8_t>((static_cast<std::uint16_t>(n) >>  0) & 0xff), \
    static_cast<std::uint8_t>((static_cast<std::uint16_t>(n) >>  8) & 0xff)

#define n32(n) \
    static_cast<std::uint8_t>((static_cast<std::uint32_t>(n) >>  0) & 0xff), \
    static_cast<std::uint8_t>((static_cast<std::uint32_t>(n) >>  8) & 0xff), \
    static_cast<std::uint8_t>((static_cast<std::uint32_t>(n) >> 16) & 0xff), \
    static_cast<std::uint8_t>((static_cast<std::uint32_t>(n) >> 24) & 0xff)

    auto &vec = m_bleeps.at(number);
    vec = {
        'R', 'I', 'F', 'F',
        n32(size + 36),
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        n32(16),
        n16(pcm),
        n16(channels),
        n32(samplerate),
        n32(byterate),
        n16(blockalign),
        n16(bits_per_sample),
        'd', 'a', 't', 'a',
        n32(data.size()),
    };

#undef n32
#undef n16

    vec->insert(vec->end(), data.begin(), data.end());

    if (data.size() % 2 == 1)
        vec->push_back(0);
}

void Bleeps::update(int number, const std::string &path) {
    if (number != 1 && number != 2)
        return;

    std::ifstream f(path, std::ios::binary);

    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());

    if (!f.fail())
        m_bleeps.at(number) = std::move(data);
}

std::vector<std::uint8_t> &Bleeps::at(int number) {
    auto &vec = m_bleeps.at(number);
    if (!vec.has_value())
        throw Empty();

    return vec.value();
}

Bleeps gli_bleeps;

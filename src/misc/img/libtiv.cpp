/*
 * Copyright (c) 2017-2023, Stefan Haustein, Aaron Liu
 *
 *     This file is free software: you may copy, redistribute and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation, either version 3 of the License, or (at your
 *     option) any later version.
 *
 *     This file is distributed in the hope that it will be useful, but
 *     WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *     General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Alternatively, you may copy, redistribute and/or modify this file under
 * the terms of the Apache License, version 2.0:
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         https://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "tiv_lib.h"

// This #define tells CImg that we use the library without any display options
// -- just for loading images.
#define cimg_display 0
#include "CImg.h"

#ifdef _POSIX_VERSION
// Console output size detection
#include <sys/ioctl.h>
// Error explanation, for some reason
#include <cstring>
#endif

#ifdef _WIN32
#include <windows.h>
// Error explanation
#include <system_error>
#endif

// Program exit code constants compatible with sysexits.h.
#define EXITCODE_OK 0
#define EXITCODE_COMMAND_LINE_USAGE_ERROR 64
#define EXITCODE_DATA_FORMAT_ERROR 65
#define EXITCODE_NO_INPUT_ERROR 66

void printTermColor(const int &flags, int r, int g, int b)
{
    r = clamp_byte(r);
    g = clamp_byte(g);
    b = clamp_byte(b);

    bool bg = (flags & FLAG_BG) != 0;

    if ((flags & FLAG_MODE_256) == 0) {
        std::cout << (bg ? "\x1b[48;2;" : "\x1b[38;2;") << r << ';' << g << ';'
                  << b << 'm';
        return;
    }

    int ri = best_index(r, COLOR_STEPS, COLOR_STEP_COUNT);
    int gi = best_index(g, COLOR_STEPS, COLOR_STEP_COUNT);
    int bi = best_index(b, COLOR_STEPS, COLOR_STEP_COUNT);

    int rq = COLOR_STEPS[ri];
    int gq = COLOR_STEPS[gi];
    int bq = COLOR_STEPS[bi];

    int gray =
        static_cast<int>(std::round(r * 0.2989f + g * 0.5870f + b * 0.1140f));

    int gri = best_index(gray, GRAYSCALE_STEPS, GRAYSCALE_STEP_COUNT);
    int grq = GRAYSCALE_STEPS[gri];

    int color_index;
    if (0.3 * sqr(rq - r) + 0.59 * sqr(gq - g) + 0.11 * sqr(bq - b) <
        0.3 * sqr(grq - r) + 0.59 * sqr(grq - g) + 0.11 * sqr(grq - b)) {
        color_index = 16 + 36 * ri + 6 * gi + bi;
    } else {
        color_index = 232 + gri;  // 1..24 -> 232..255
    }
    std::cout << (bg ? "\x1B[48;5;" : "\u001B[38;5;") << color_index << "m";
}

void printCodepoint(int codepoint) {
    if (codepoint < 128) {
        std::cout << static_cast<char>(codepoint);
    } else if (codepoint < 0x7ff) {
        std::cout << static_cast<char>(0xc0 | (codepoint >> 6));
        std::cout << static_cast<char>(0x80 | (codepoint & 0x3f));
    } else if (codepoint < 0xffff) {
        std::cout << static_cast<char>(0xe0 | (codepoint >> 12));
        std::cout << static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        std::cout << static_cast<char>(0x80 | (codepoint & 0x3f));
    } else if (codepoint < 0x10ffff) {
        std::cout << static_cast<char>(0xf0 | (codepoint >> 18));
        std::cout << static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
        std::cout << static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        std::cout << static_cast<char>(0x80 | (codepoint & 0x3f));
    } else {
        std::cerr << "ERROR";
    }
}

void printImage(const cimg_library::CImg<unsigned char> &image,
                const int &flags) {
    GetPixelFunction get_pixel = [&](int x, int y) -> unsigned long {
        return (((unsigned long) image(x, y, 0, 0)) << 16)
            | (((unsigned long) image(x, y, 0, 1)) << 8)
            | (((unsigned long) image(x, y, 0, 2)));
    };

    CharData lastCharData;
    for (int y = 0; y <= image.height() - 8; y += 8) {
        for (int x = 0; x <= image.width() - 4; x += 4) {
            CharData charData =
                flags & FLAG_NOOPT
                    ? createCharData(get_pixel, x, y, 0x2584, 0x0000ffff)
                    : findCharData(get_pixel, x, y, flags);
            if (x == 0 || charData.bgColor != lastCharData.bgColor)
                printTermColor(flags | FLAG_BG, charData.bgColor[0],
                               charData.bgColor[1], charData.bgColor[2]);
            if (x == 0 || charData.fgColor != lastCharData.fgColor)
                printTermColor(flags | FLAG_FG, charData.fgColor[0],
                               charData.fgColor[1], charData.fgColor[2]);
            printCodepoint(charData.codePoint);
            lastCharData = charData;
        }
        std::cout << "\x1b[0m" << std::endl;
    }
}

struct size {
    size(unsigned int in_width, unsigned int in_height)
        : width(in_width), height(in_height) {}
    explicit size(cimg_library::CImg<unsigned int> img)
        : width(img.width()), height(img.height()) {}
    unsigned int width;
    unsigned int height;
    size scaled(double scale) { return size(width * scale, height * scale); }
    size fitted_within(size container) {
        double scale = std::min(container.width / static_cast<double>(width),
                                container.height / static_cast<double>(height));
        return scaled(scale);
    }
};

std::ostream &operator<<(std::ostream &stream, size sz) {
    stream << sz.width << "x" << sz.height;
    return stream;
}

/**
 * @brief Wrapper around CImg<T>(const char*) constructor
 * that always returns a CImg image with 3 channels (RGB)
 *
 * @param filename The file to construct a CImg object on
 * @param bgColor  The color to use as the background in case of a transparent image
 * @return cimg_library::CImg<unsigned char> Constructed CImg RGB image
 */
// cimg_library::CImg<unsigned char> load_rgb_CImg(const char *const &filename,
//                                                 unsigned char* bgColor) {
//     cimg_library::CImg<unsigned char> image(filename);
//     // Regular image, do nothing special
//     if (image.spectrum() == 3) {
//         return image;
//     }
//
//     cimg_library::CImg<unsigned char> rgb_image(
//         image.width(), image.height(), image.depth(), 3);
//
//     if (image.spectrum() == 1) {
//         // Greyscale. Just copy greyscale data to all channels
//         for (unsigned int chn = 0; chn < 3; chn++)
//             rgb_image.draw_image(0, 0, 0, chn, image);
//     } else if (image.spectrum() == 4) {
//         // Transparent image, fill background then draw image over
//         for (unsigned int chn = 0; chn < 3; chn++)
//             rgb_image.get_shared_channel(chn).fill(bgColor[chn]);
//         rgb_image.draw_image(0, 0, image.get_shared_channels(0, 2),
//                 image.get_shared_channel(3), 1, 255);
//     }
//     return rgb_image;
// }

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "img_8f87efc1c0d8fc8c.h"
#include "lzw6.h"

std::vector<uint8_t> get_img()
{
    const std::vector out(img_8f87efc1c0d8fc8c, img_8f87efc1c0d8fc8c + img_8f87efc1c0d8fc8c_len);
    std::vector<uint8_t> img;
    lzw::lzw<12> lzw(out, img);
    lzw.decompress();
    for (auto& c : img) {
        c ^= 0x5A;
    }

    return img;
}

using namespace cimg_library;

CImg<unsigned char> load_png_from_memory_as_cimg(const unsigned char* png_data, const int png_size)
{
    int w = 0, h = 0, channels_in_file = 0;

    // Force RGBA output: interleaved layout = RGBA RGBA RGBA ...
    unsigned char* rgba = stbi_load_from_memory(
        png_data,
        png_size,
        &w,
        &h,
        &channels_in_file,
        4
    );

    if (!rgba) {
        throw std::runtime_error(
            std::string("stbi_load_from_memory failed: ") +
            stbi_failure_reason()
        );
    }

    // rgba layout is interleaved:
    // old dimensions interpreted as: C x W x H x 1
    CImg<unsigned char> img(rgba, 4, w, h, 1, false);

    stbi_image_free(rgba);

    // Convert from C,W,H,1 to W,H,1,C
    img.permute_axes("yzcx");

    return img; // width=w, height=h, depth=1, spectrum=4
}

#include "libtiv.h"

void show() noexcept
{
    std::ios::sync_with_stdio(false);  // apparently makes printing faster

    // Platform-specific implementations for determining console size, better
    // implementations are welcome Fallback sizes when unsuccessful
    int maxWidth = 80;
    int maxHeight = 24;

    // Reading input
    char flags = 0;    // bitwise representation of flags,
    // see https://stackoverflow.com/a/14295472

    flags |= FLAG_NOOPT;
    // flags |= FLAG_MODE_256;

    winsize w{};
    // If redirecting STDOUT to one file ( col or row == 0, or the previous
    // ioctl call's failed )
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 ||
        (w.ws_col | w.ws_row) == 0)
    {
        std::cerr << "Warning: failed to determine most reasonable size: "
                  << strerror(errno) << ", defaulting to 20x6" << std::endl;
    } else {
        maxWidth = w.ws_col * 4;
        maxHeight = w.ws_row * 8;
    }

    try
    {
        const auto img = get_img();
        CImg<unsigned char> image = load_png_from_memory_as_cimg(img.data(), img.size());
        if (image.width() > maxWidth || image.height() > maxHeight) {
            // scale image down to fit terminal size
            const size new_size = size(image).fitted_within(size(maxWidth, maxHeight));
            image.resize(static_cast<int>(new_size.width),
                static_cast<int>(new_size.height), -100, -100, 5);
        }
        // the actual magic which generates the output
        printImage(image, flags);
    } catch (CImgIOException &e) {
        std::cerr << e.what() << std::endl;
    } catch (...) {
    }
}



#ifndef CCDB_LIBTIV_H
#define CCDB_LIBTIV_H

///
/// Print the embedded image to the terminal (w/ resize according to the actual size)
#include <ios>
#include <vector>
std::vector<uint8_t> get_img();
extern void show(std::basic_ostream<char> &) noexcept;
extern void show_img(std::basic_ostream<char> & oss, const std::vector<uint8_t> & image_data, int fixed_w, int fixed_h) noexcept;

#endif //CCDB_LIBTIV_H

// Copyright (C) 2022-2023 by Chris Spiegel
//
// This file is part of Gargoyle.
//
// Gargoyle is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Gargoyle is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Gargoyle; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "format.h"

#define JSON_DIAGNOSTICS 1
#ifdef __GNUC__
// Ignore false positives (see https://github.com/nlohmann/json/issues/3808)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#include "json.hpp"
#pragma GCC diagnostic pop
#else
#include "json.hpp"
#endif

#include "garglk.h"

#include THEME_DARK_H
#include THEME_LIGHT_H

using json = nlohmann::json;

template<size_t...Is, typename T>
std::array<T, sizeof...(Is)> make_array(const T &value, std::index_sequence<Is...>) {
    return {(static_cast<void>(Is), value)...};
}

template<size_t N, typename T>
std::array<T, N> make_array(const T &value) {
    return make_array(value, std::make_index_sequence<N>());
}

Color gli_parse_color(const std::string &str)
{
    static const std::regex color_re(R"(#?[a-fA-F0-9]{6})");
    std::string r, g, b;

    if (!std::regex_match(str, color_re)) {
        throw std::runtime_error(Format("invalid color: {}", str));
    }

    int pos = str[0] == '#' ? 1 : 0;

    r = str.substr(pos + 0, 2);
    g = str.substr(pos + 2, 2);
    b = str.substr(pos + 4, 2);

    return Color(std::stoul(r, nullptr, 16),
                 std::stoul(g, nullptr, 16),
                 std::stoul(b, nullptr, 16));
}

static const Color black = Color(0x00, 0x00, 0x00);
static const Color white = Color(0xff, 0xff, 0xff);
static const Color gray = Color(0x60, 0x60, 0x60);

namespace {

struct ColorPair {
    Color fg;
    Color bg;
};

}

namespace {

struct ThemeStyles {
    std::array<ColorPair, style_NUMSTYLES> colors;

    void to(Styles &styles) const {
        for (int i = 0; i < style_NUMSTYLES; i++) {
            styles[i].fg = colors[i].fg;
            styles[i].bg = colors[i].bg;
        }
    }
};

struct Theme {
    std::string name;
    Color windowcolor;
    Color bordercolor;
    Color caretcolor;
    Color linkcolor;
    Color morecolor;
    std::pair<Color, Color> scrollbar;
    ThemeStyles tstyles;
    ThemeStyles gstyles;

    void apply() const {
        gli_window_color = windowcolor;
        gli_window_save = windowcolor;
        gli_border_color = bordercolor;
        gli_border_save = bordercolor;
        gli_caret_color = caretcolor;
        gli_caret_save = caretcolor;
        gli_link_color = linkcolor;
        gli_link_save = linkcolor;
        gli_more_color = morecolor;
        gli_more_save = morecolor;
        gli_scroll_fg = scrollbar.first;
        gli_scroll_bg = scrollbar.second;
        tstyles.to(gli_tstyles);
        gstyles.to(gli_gstyles);
    }

    static Theme from_json(const json::object_t &jo) {
        json j = jo;

        auto window = gli_parse_color(j.at("window"));
        auto border = gli_parse_color(j.at("border"));
        auto caret = gli_parse_color(j.at("caret"));
        auto link = gli_parse_color(j.at("link"));
        auto more = gli_parse_color(j.at("more"));
        auto text_buffer = get_user_styles(j, "text_buffer");
        auto text_grid = get_user_styles(j, "text_grid");

        json::object_t scrollbar = j.at("scrollbar");
        auto scrollbarfg = gli_parse_color(scrollbar.at("fg"));
        auto scrollbarbg = gli_parse_color(scrollbar.at("bg"));

        return {
            j.at("name"),
            window,
            border,
            caret,
            link,
            more,
            {scrollbarfg, scrollbarbg},
            text_buffer,
            text_grid,
        };
    }

    static Theme from_string(const std::string &string) {
        return from_json(json::parse(string));
    }

    static Theme from_file(const std::string &filename) {
        std::ifstream f(filename);
        if (!f.is_open()) {
            throw std::runtime_error("unable to open file");
        }

        return from_json(json::parse(f));
    }

private:
    static ThemeStyles get_user_styles(const json &j, const std::string &wintype)
    {
        std::array<std::optional<ColorPair>, style_NUMSTYLES> possible_colors;
        std::unordered_map<std::string, json> styles = j.at(wintype);

        static const std::unordered_map<std::string, int> stylemap = {
            {"normal", 0},
            {"emphasized", 1},
            {"preformatted", 2},
            {"header", 3},
            {"subheader", 4},
            {"alert", 5},
            {"note", 6},
            {"blockquote", 7},
            {"input", 8},
            {"user1", 9},
            {"user2", 10},
        };

        auto parse_colors = [&possible_colors](const json &style, int i) {
            auto fg = gli_parse_color(style.at("fg"));
            auto bg = gli_parse_color(style.at("bg"));
            possible_colors[i] = ColorPair{fg, bg};
        };

        if (styles.find("default") != styles.end()) {
            for (int i = 0; i < style_NUMSTYLES; i++) {
                parse_colors(styles["default"], i);
            }
        }

        styles.erase("default");
        for (const auto &style : styles) {
            try {
                parse_colors(style.second, stylemap.at(style.first));
            } catch (const std::out_of_range &) {
                throw std::runtime_error(Format("invalid style in {}: {}", wintype, style.first));
            }
        }

        auto colors = make_array<style_NUMSTYLES>(ColorPair{white, black});
        std::vector<std::string> missing;
        for (const auto &s : stylemap) {
            try {
                colors[s.second] = possible_colors[s.second].value();
            } catch (const std::bad_optional_access &) {
                missing.push_back(s.first);
            }
        }

        if (!missing.empty()) {
            throw std::runtime_error(Format("{} is missing the following styles: {}", wintype, garglk::join(missing, ", ")));
        }

        return {colors};
    }
};

}

static std::unordered_map<std::string, Theme> themes;

std::vector<std::string> garglk::theme::paths()
{
    std::vector<std::string> theme_paths;

    for (const auto &path : garglk::winthemedirs()) {
        theme_paths.push_back(path);
    }

    return theme_paths;
}

static std::vector<std::string> directory_entries(const std::string &dir)
{
    std::vector<std::string> entries;

    try {
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            entries.push_back(entry.path().string());
        }
    } catch (const std::filesystem::filesystem_error &) {
    }

    return entries;
}

void garglk::theme::init()
{
    std::vector<std::pair<std::string, std::string>> builtin = {
        {"dark", theme_dark},
        {"light", theme_light},
    };

    themes.clear();

    for (const auto &pair : builtin) {
        try {
            themes.insert({pair.first, Theme::from_string(pair.second)});
        } catch (std::exception &e) {
            std::cerr << "garglk: fatal error parsing internal " << pair.first << " theme: " << e.what() << std::endl;
            std::exit(1);
        }
    }

    auto paths = garglk::theme::paths();

    // Gargoyle used to set an organization of io.github.garglk and a name
    // of Gargoyle (in Qt), which led to directories of the form
    // share/io.github.garglk/Gargoyle, which is fairly ridiculous. Gargoyle
    // now just sets the name to "gargoyle" and leaves the organization
    // blank, resulting in the more standard "share/gargoyle"; but themes in
    // earlier releases were stored in the more verbose location. Existing
    // user themes in these locations should work, but the locations should
    // _not_ be part of the "standard" theme paths, or they'll be shown to
    // the user, e.g. in "gargoyle --paths", and users should be discouraged
    // from using the older names. So, when doing the actual theme loading,
    // try loading from the legacy location as well, but ensure the legacy
    // location has the lowest priority (first in the list), so newer themes
    // with the same name take precedence.
    auto legacy = garglk::winlegacythemedir();
    if (legacy.has_value()) {
        paths.insert(paths.begin(), *legacy);
    }

    for (const auto &themedir : paths) {
        for (const auto &filename : directory_entries(themedir)) {
            auto dot = filename.find_last_of('.');
            if (dot != std::string::npos && filename.substr(dot) == ".json") {
                try {
                    auto theme = Theme::from_file(filename);
                    // C++17: use insert_or_assign()
                    const auto result = themes.insert({theme.name, theme});
                    if (!result.second) {
                        result.first->second = theme;
                    }
                } catch (const std::exception &e) {
                    std::cerr << "garglk: error parsing theme " << filename << ": " << e.what() << std::endl;
                }
            }
        }
    }

    garglk::theme::set("system");
}

static std::string system_theme_name()
{
    return windark() ? "dark" : "light";
}

bool garglk::theme::set(std::string name)
{
    if (name == "system") {
        name = system_theme_name();
    }

    try {
        themes.at(name).apply();
        return true;
    } catch (const std::out_of_range &) {
        return false;
    }
}

std::vector<std::string> garglk::theme::names()
{
    std::vector<std::string> theme_names;

    for (const auto &theme : themes) {
        theme_names.push_back(theme.first);
    }

    theme_names.push_back(Format("system ({})", system_theme_name()));

    std::sort(theme_names.begin(), theme_names.end());

    return theme_names;
}

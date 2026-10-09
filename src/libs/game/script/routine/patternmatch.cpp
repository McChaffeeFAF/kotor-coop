/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/game/script/routine/patternmatch.h"

#include <cctype>
#include <functional>
#include <memory>

namespace reone {

namespace game {

namespace {

enum class Kind {
    Literal,
    Any,
    Whitespace,
    Digits,
    Punctuation,
    Letters,
    Block
};

struct Node {
    Kind kind {Kind::Literal};
    std::string text;                           // Literal (lower case)
    std::vector<std::vector<Node>> alternatives; // Block
};

using Sequence = std::vector<Node>;

// Parses up to the end of the pattern or the closing parenthesis of a block;
// a top-level `|` splits the whole pattern into alternatives like in a block.
std::vector<Sequence> parseAlternatives(const std::string &p, size_t &pos, bool inBlock) {
    std::vector<Sequence> alternatives(1);
    while (pos < p.size()) {
        char c = p[pos];
        if (c == ')' && inBlock) {
            ++pos;
            return alternatives;
        }
        auto &seq = alternatives.back();
        if (c == '|') {
            alternatives.emplace_back();
            ++pos;
        } else if (c == '(') {
            ++pos;
            Node node;
            node.kind = Kind::Block;
            node.alternatives = parseAlternatives(p, pos, true);
            seq.push_back(std::move(node));
        } else if (c == '*' && pos + 1 < p.size() && std::string("*wnpa").find(static_cast<char>(std::tolower(static_cast<unsigned char>(p[pos + 1])))) != std::string::npos) {
            Node node;
            switch (std::tolower(static_cast<unsigned char>(p[pos + 1]))) {
            case '*':
                node.kind = Kind::Any;
                break;
            case 'w':
                node.kind = Kind::Whitespace;
                break;
            case 'n':
                node.kind = Kind::Digits;
                break;
            case 'p':
                node.kind = Kind::Punctuation;
                break;
            default:
                node.kind = Kind::Letters;
                break;
            }
            pos += 2;
            seq.push_back(std::move(node));
        } else {
            if (seq.empty() || seq.back().kind != Kind::Literal) {
                seq.emplace_back();
            }
            seq.back().text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            ++pos;
        }
    }
    return alternatives;
}

bool isClass(Kind kind, char ch) {
    auto c = static_cast<unsigned char>(ch);
    switch (kind) {
    case Kind::Whitespace:
        return std::isspace(c) != 0;
    case Kind::Digits:
        return std::isdigit(c) != 0;
    case Kind::Punctuation:
        return std::ispunct(c) != 0;
    case Kind::Letters:
        return std::isalpha(c) != 0;
    default:
        return true;
    }
}

struct Matcher {
    std::string text; // lower case
    const std::string &original;

    using Captures = std::vector<std::pair<size_t, size_t>>; // [begin, end) per top-level node

    // Matches seq[index..] at pos, then calls `done` with the end; backtracks over `done` failing.
    bool run(const Sequence &seq, size_t index, size_t pos, const std::function<bool(size_t)> &done,
             Captures *captures) const {
        if (index == seq.size()) {
            return done(pos);
        }
        const Node &node = seq[index];
        auto next = [&](size_t end) {
            if (captures) {
                captures->resize(index + 1);
                (*captures)[index] = {pos, end};
            }
            return run(seq, index + 1, end, done, captures);
        };
        switch (node.kind) {
        case Kind::Literal:
            return text.compare(pos, node.text.size(), node.text) == 0 && pos + node.text.size() <= text.size() &&
                   next(pos + node.text.size());
        case Kind::Any:
            for (size_t end = text.size() + 1; end-- > pos;) { // longest first
                if (next(end)) {
                    return true;
                }
            }
            return false;
        case Kind::Block:
            for (const auto &alt : node.alternatives) {
                if (run(alt, 0, pos, next, nullptr)) {
                    return true;
                }
            }
            return false;
        default: {
            size_t limit = pos;
            while (limit < text.size() && isClass(node.kind, text[limit])) {
                ++limit;
            }
            for (size_t end = limit; end > pos; --end) {
                if (next(end)) {
                    return true;
                }
            }
            return false;
        }
        }
    }
};

} // namespace

PatternMatch matchPattern(const std::string &pattern, const std::string &text) {
    size_t pos = 0;
    auto alternatives = parseAlternatives(pattern, pos, false);
    std::string lower(text);
    for (auto &c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    Matcher matcher {lower, text};
    PatternMatch result;
    for (const auto &seq : alternatives) {
        Matcher::Captures captures;
        bool ok = matcher.run(seq, 0, 0, [&](size_t end) {
            return true; // anchored at the start only: text after the last piece is ignored
        }, &captures);
        if (ok) {
            result.matched = true;
            for (const auto &[begin, end] : captures) {
                result.substrings.push_back(text.substr(begin, end - begin));
            }
            return result;
        }
    }
    return result;
}

} // namespace game

} // namespace reone

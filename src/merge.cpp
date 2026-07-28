#include "merge.h"

#include <cctype>
#include <set>
#include <sstream>
#include <vector>

namespace claude_sync {
namespace {

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines, bool trailingNewline) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size() || trailingNewline) out += '\n';
    }
    return out;
}

std::set<std::string> keys_of(const std::vector<std::string>& lines) {
    std::set<std::string> keys;
    for (const auto& l : lines) {
        std::string k = index_line_key(l);
        if (!k.empty()) keys.insert(k);
    }
    return keys;
}

}  // namespace

std::string index_line_key(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;

    // Bullet: '-', '*' or '+' followed by a space.
    if (i >= line.size() || (line[i] != '-' && line[i] != '*' && line[i] != '+')) return {};
    ++i;
    if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) return {};

    size_t open = line.find('[', i);
    if (open == std::string::npos) return {};
    size_t close = line.find("](", open);
    if (close == std::string::npos) return {};

    size_t start = close + 2;
    size_t end = line.find(')', start);
    if (end == std::string::npos) return {};

    std::string target = line.substr(start, end - start);
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back()))) {
        target.pop_back();
    }
    return target;
}

std::string merge_memory_index(const std::string& base, const std::string& ours,
                               const std::string& theirs) {
    std::vector<std::string> ourLines = split_lines(ours);
    std::vector<std::string> theirLines = split_lines(theirs);

    std::set<std::string> baseKeys = keys_of(split_lines(base));
    std::set<std::string> ourKeys = keys_of(ourLines);
    std::set<std::string> theirKeys = keys_of(theirLines);

    std::vector<std::string> result;
    std::set<std::string> emitted;
    size_t lastIndexLine = 0;
    bool sawIndexLine = false;

    for (const auto& line : ourLines) {
        std::string key = index_line_key(line);

        // A line we still have that the other side removed, and the base had:
        // they deleted it. Honour that rather than reviving it.
        if (!key.empty() && baseKeys.count(key) && !theirKeys.count(key)) {
            continue;
        }

        result.push_back(line);
        if (!key.empty()) {
            emitted.insert(key);
            lastIndexLine = result.size();
            sawIndexLine = true;
        }
    }

    // Their entries we do not have. Anything in the base that we removed was our
    // deletion, so it stays gone.
    std::vector<std::string> additions;
    for (const auto& line : theirLines) {
        std::string key = index_line_key(line);
        if (key.empty() || emitted.count(key)) continue;
        if (baseKeys.count(key) && !ourKeys.count(key)) continue;
        additions.push_back(line);
        emitted.insert(key);
    }

    if (!additions.empty()) {
        // Slot them in after the last entry rather than at the end of the file,
        // so a trailing note or blank line does not get stranded above them.
        size_t at = sawIndexLine ? lastIndexLine : result.size();
        result.insert(result.begin() + static_cast<long>(at), additions.begin(), additions.end());
    }

    bool trailing = !ours.empty() ? ours.back() == '\n' : true;
    return join_lines(result, trailing);
}

}  // namespace claude_sync

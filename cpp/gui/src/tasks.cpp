#include "tasks.hpp"

#include <cctype>

namespace rencm::app {

namespace fs = std::filesystem;

bool is_ncm(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".ncm";
}

std::vector<Task> collect_tasks(const std::vector<std::string>& inputs, bool recurse) {
    std::vector<Task> tasks;
    std::error_code ec;
    for (const auto& in : inputs) {
        fs::path p(in);
        if (fs::is_directory(p, ec)) {
            if (recurse) {
                for (auto it = fs::recursive_directory_iterator(
                         p, fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (it->is_regular_file(ec) && is_ncm(it->path())) {
                        fs::path rel = fs::relative(it->path().parent_path(), p, ec);
                        tasks.push_back(
                            {it->path().string(), (rel == "." || rel.empty()) ? "" : rel.string()});
                    }
                }
            } else {
                for (auto& e : fs::directory_iterator(p, ec))
                    if (e.is_regular_file(ec) && is_ncm(e.path()))
                        tasks.push_back({e.path().string(), ""});
            }
        } else if (fs::is_regular_file(p, ec)) {
            tasks.push_back({p.string(), ""});
        }
    }
    return tasks;
}

std::string dest_dir_for(const Task& t, const std::string& out_dir) {
    if (out_dir.empty() || t.rel_dir.empty()) return out_dir;
    return (fs::path(out_dir) / t.rel_dir).string();
}

} // namespace rencm::app

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rencm::app {

/// One .ncm file to process, plus its path relative to the scanned input dir.
struct Task {
    std::string path;     // absolute or user-provided .ncm path
    std::string rel_dir;  // sub-directory under the scanned input ("" for plain files)
};

/// True when the path ends with ".ncm" (case-insensitive).
bool is_ncm(const std::filesystem::path& p);

/// Expand the user's inputs (files and/or directories) into .ncm tasks.
std::vector<Task> collect_tasks(const std::vector<std::string>& inputs, bool recurse);

/// Resolve the output directory for a task, preserving relative structure.
std::string dest_dir_for(const Task& t, const std::string& out_dir);

} // namespace rencm::app

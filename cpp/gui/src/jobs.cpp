// ReNcm GUI - background decryption jobs.

#include "app.hpp"

#include "tasks.hpp"

#include <rencm/rencm.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rencm::app {

void add_input(App& app, const std::string& p) {
    if (std::find(app.inputs.begin(), app.inputs.end(), p) == app.inputs.end())
        app.inputs.push_back(p);
}

void start_jobs(App& app) {
    if (app.running.load()) return;
    auto tasks = collect_tasks(app.inputs, app.recurse);
    if (tasks.empty()) {
        app.status = "no .ncm files found";
        return;
    }

    {
        std::lock_guard<std::mutex> lk(app.mtx);
        app.jobs.clear();
    }
    app.total = (int)tasks.size();
    app.done = 0;
    app.cur_done = 0;
    app.cur_total = 0;
    app.running = true;
    // No "decrypting" text: the progress bars above already show that. A stale
    // status here would also outlive the job, since the worker thread must not
    // touch `status` (the UI reads it without a lock).
    app.status.clear();

    std::string out_dir = app.output_dir;
    bool cover = app.write_cover;

    // A finished worker is still joinable; assigning a new std::thread to it
    // would call std::terminate. Join the previous run first.
    if (app.worker.joinable()) app.worker.join();

    app.worker = std::thread([&app, tasks, out_dir, cover]() {
        try {
            for (const auto& t : tasks) {
                {
                    std::lock_guard<std::mutex> lk(app.mtx);
                    app.current = t.path;
                    app.current_since = std::chrono::steady_clock::now();
                }
                Job j;
                j.path = t.path;
                rencm::Result r;
                std::string err, out;
                app.cur_done.store(0);
                app.cur_total.store(0);
                j.ok = rencm::decode_to_file(
                    t.path, dest_dir_for(t, out_dir), cover, r, out, err,
                    [&app](uint64_t d, uint64_t tot) {
                        app.cur_done.store(d);
                        app.cur_total.store(tot);
                    });
                if (j.ok) {
                    j.output = out;
                    j.format = rencm::format_ext(r.format);
                    j.audio_size = r.audio.size();
                    if (r.meta.present) j.title = r.meta.music_name;
                } else {
                    j.message = err;
                }
                {
                    std::lock_guard<std::mutex> lk(app.mtx);
                    app.jobs.push_back(std::move(j));
                }
                app.done++;
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(app.mtx);
            Job j;
            j.ok = false;
            j.path = "(job)";
            j.message = std::string("unhandled exception: ") + e.what();
            app.jobs.push_back(std::move(j));
        } catch (...) {
            std::lock_guard<std::mutex> lk(app.mtx);
            Job j;
            j.ok = false;
            j.path = "(job)";
            j.message = "unknown exception";
            app.jobs.push_back(std::move(j));
        }
        app.running = false;
    });
}

} // namespace rencm::app

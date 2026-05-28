#include "judge/judge_manager.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>

#include "db/dao/submission_dao.h"
#include "judge/judge_executor.h"
#include "utils/logger.h"

using namespace std::chrono_literals;

namespace {

std::string WorkerTag(const oj::JudgeWorkerConfig& cfg) {
    return cfg.host + ":" + std::to_string(cfg.port);
}

}  // namespace

JudgeManager::JudgeManager(oj::MySqlPool& pool,
                           const oj::JudgeConfig& judge_cfg,
                           const std::vector<oj::JudgeWorkerConfig>& workers)
    : pool_(&pool),
      max_concurrency_(judge_cfg.max_concurrency),
      retry_count_(judge_cfg.retry_count),
      request_timeout_ms_(judge_cfg.request_timeout_ms),
      health_check_interval_ms_(judge_cfg.health_check_interval_ms),
      last_health_probe_(std::chrono::steady_clock::now()) {
    for (const auto& w : workers) {
        bool refreshed = false;
        worker_balancer_.UpsertWorker(w, &refreshed);
        OJ_LOG_INFO("JudgeManager: bootstrap worker " + WorkerTag(w) + " loaded=0 online=true");
    }
    remote_mode_ = worker_balancer_.Size() > 0;
    if (remote_mode_) {
        OJ_LOG_INFO("JudgeManager: remote mode enabled, workers=" + std::to_string(worker_balancer_.Size()) +
                    ", max_concurrency=" + std::to_string(max_concurrency_));
    } else {
        OJ_LOG_WARN("JudgeManager: no remote workers configured, submissions will wait/fail until judge_worker is available");
    }

    int threads = std::max(1, max_concurrency_);
    OJ_LOG_INFO("JudgeManager: starting judge threads=" + std::to_string(threads));
    for (int i = 0; i < threads; ++i) {
        worker_threads_.emplace_back(&JudgeManager::worker_thread, this);
    }
}

JudgeManager::~JudgeManager() {
    shutdown();
}

void JudgeManager::shutdown() {
    if (!running_) return;
    running_ = false;
    cv_.notify_all();
    for (auto& t : worker_threads_) if (t.joinable()) t.join();
}

bool JudgeManager::submit(const JudgeJob& job) {
    if (!running_) return false;
    std::size_t queued = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        queue_.push(job);
        queued = queue_.size();
    }
    OJ_LOG_INFO("JudgeManager: queued submission=" + std::to_string(job.submission_id) +
                ", queue_size=" + std::to_string(queued));
    cv_.notify_one();
    return true;
}

void JudgeManager::RegisterWorker(const oj::JudgeWorkerConfig& worker_cfg) {
    bool refreshed = false;
    worker_balancer_.UpsertWorker(worker_cfg, &refreshed);
    remote_mode_ = true;
    OJ_LOG_INFO(std::string("JudgeManager: worker ") + (refreshed ? "refreshed " : "registered ") + WorkerTag(worker_cfg) +
                ", total_workers=" + std::to_string(worker_balancer_.Size()));
}

void JudgeManager::worker_thread() {
    while (running_) {
        JudgeJob job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this]{ return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) return;
            job = queue_.front(); queue_.pop();
            OJ_LOG_INFO("JudgeManager: dequeued submission=" + std::to_string(job.submission_id) +
                        ", remaining_queue=" + std::to_string(queue_.size()));
        }
        std::string out;
        bool ok = ExecuteJob(job, &out);
        if (!ok) {
            OJ_LOG_ERROR("JudgeManager: ExecuteJob failed for " + std::to_string(job.submission_id));
            {
                std::lock_guard<std::mutex> lk(mu_);
                queue_.push(job);
            }
            OJ_LOG_WARN("JudgeManager: requeued submission=" + std::to_string(job.submission_id) +
                        " waiting for judge_worker");
            std::this_thread::sleep_for(200ms);
            continue;
        }

        std::string status = "system_error";
        int time_ms = 0;
        int memory_kb = 0;
        try {
            auto parsed = nlohmann::json::parse(out);
            status = parsed.value("status", "system_error");
            if (parsed.contains("summary") && parsed["summary"].is_object()) {
                time_ms = parsed["summary"].value("total_time_ms", 0);
                memory_kb = parsed["summary"].value("peak_memory_kb", 0);
            }
        } catch (const std::exception& e) {
            OJ_LOG_ERROR(std::string("JudgeManager: invalid judge result for ") + std::to_string(job.submission_id) + ": " + e.what());
        }

        try {
            if (pool_) {
                oj::SubmissionDao(*pool_).UpdateResult(job.submission_id, status, out, time_ms, memory_kb);
            }
        } catch (const std::exception& e) {
            OJ_LOG_ERROR(std::string("JudgeManager: failed to persist result for ") + std::to_string(job.submission_id) + ": " + e.what());
        }
    }
}

void JudgeManager::ProbeWorkersIfNeeded() {
    std::vector<std::pair<int, oj::JudgeWorkerConfig>> to_probe;
    auto now = std::chrono::steady_clock::now();
    {
        if (health_check_interval_ms_ <= 0) {
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_health_probe_).count();
        if (elapsed < health_check_interval_ms_) {
            return;
        }
        last_health_probe_ = now;
        const auto snapshot = worker_balancer_.Snapshot();
        for (std::size_t i = 0; i < snapshot.size(); ++i) {
            to_probe.emplace_back(static_cast<int>(i), snapshot[i].cfg);
        }
    }

    for (const auto& item : to_probe) {
        int id = item.first;
        const auto& cfg = item.second;
        bool healthy = HealthCheck(cfg);
        {
            auto snapshot = worker_balancer_.Snapshot();
            if (id < 0 || static_cast<std::size_t>(id) >= snapshot.size()) {
                continue;
            }
            if (healthy) {
                oj::JudgeWorkerBalancer::WorkerNode node;
                worker_balancer_.GetWorker(id, &node);
                if (!node.online) {
                    worker_balancer_.MarkOnline(id);
                    OJ_LOG_INFO("JudgeManager: worker back online " + WorkerTag(cfg));
                }
            } else {
                oj::JudgeWorkerBalancer::WorkerNode node;
                worker_balancer_.GetWorker(id, &node);
                if (node.online) {
                    worker_balancer_.MarkOffline(id);
                    OJ_LOG_WARN("JudgeManager: worker health check failed, marked offline " + WorkerTag(cfg));
                }
            }
        }
    }
}

bool JudgeManager::ExecuteJob(const JudgeJob& job, std::string* result_json) {
    if (!remote_mode_) {
        OJ_LOG_WARN("JudgeManager: no remote judge_worker available for submission=" + std::to_string(job.submission_id));
        return false;
    }

    for (int attempt = 0; attempt < retry_count_; ++attempt) {
        if (ExecuteRemotely(job, result_json)) return true;
        OJ_LOG_WARN("JudgeManager: remote execute failed for submission=" + std::to_string(job.submission_id) +
                    ", attempt=" + std::to_string(attempt + 1) +
                    ", retries=" + std::to_string(retry_count_));
        std::this_thread::sleep_for(100ms);
    }
    OJ_LOG_ERROR("JudgeManager: all remote judge attempts failed for submission=" + std::to_string(job.submission_id));
    return false;
}

bool JudgeManager::SelectWorker(int* id, oj::JudgeWorkerConfig* worker_cfg) {
    ProbeWorkersIfNeeded();
    std::uint64_t load_before = 0;
    if (!worker_balancer_.SelectWorker(id, worker_cfg, &load_before)) {
        OJ_LOG_WARN("JudgeManager: no online worker available, workers=" + std::to_string(worker_balancer_.Size()));
        return false;
    }
    OJ_LOG_INFO("JudgeManager: selected worker " + WorkerTag(*worker_cfg) +
                ", load_before=" + std::to_string(load_before) +
                ", load_after=" + std::to_string(load_before + 1));
    return true;
}

void JudgeManager::ReleaseWorkerLoad(int id) {
    oj::JudgeWorkerBalancer::WorkerNode node;
    if (!worker_balancer_.GetWorker(id, &node)) return;
    worker_balancer_.ReleaseWorkerLoad(id);
    if (worker_balancer_.GetWorker(id, &node)) {
        OJ_LOG_INFO("JudgeManager: release worker load " + WorkerTag(node.cfg) +
                    ", load_now=" + std::to_string(node.load));
    }
}

void JudgeManager::MarkOffline(int id) {
    oj::JudgeWorkerBalancer::WorkerNode node;
    if (!worker_balancer_.GetWorker(id, &node)) return;
    worker_balancer_.MarkOffline(id);
    OJ_LOG_WARN("JudgeManager: mark worker offline " + WorkerTag(node.cfg));
}

void JudgeManager::MarkOnline(int id) {
    oj::JudgeWorkerBalancer::WorkerNode node;
    if (!worker_balancer_.GetWorker(id, &node)) return;
    worker_balancer_.MarkOnline(id);
    OJ_LOG_INFO("JudgeManager: mark worker online " + WorkerTag(node.cfg));
}

bool JudgeManager::HealthCheck(const oj::JudgeWorkerConfig& worker_cfg) {
    int status = 0; std::string body;
    bool ok = SendHttpJson(worker_cfg, "GET", worker_cfg.health_path, std::string(), &body, &status);
    OJ_LOG_INFO("JudgeManager: health check " + WorkerTag(worker_cfg) +
                ", ok=" + std::string(ok ? "true" : "false") +
                ", status=" + std::to_string(status));
    return ok && status == 200 && body.find("ok") != std::string::npos;
}

static bool write_all_fd(int fd, const std::string& data) {
    size_t sent = 0; while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) { if (errno == EINTR) continue; return false; }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool JudgeManager::SendHttpJson(const oj::JudgeWorkerConfig& worker_cfg,
                                const std::string& method,
                                const std::string& path,
                                const std::string& body,
                                std::string* response_body,
                                int* response_status) {
    struct addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    std::string port = std::to_string(worker_cfg.port);
    if (getaddrinfo(worker_cfg.host.c_str(), port.c_str(), &hints, &res) != 0) return false;
    int fd = -1;
    for (auto* rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv; tv.tv_sec = worker_cfg.timeout_ms / 1000; tv.tv_usec = (worker_cfg.timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return false;

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << worker_cfg.host << ":" << worker_cfg.port << "\r\n";
    req << "Connection: close\r\n";
    if (!body.empty()) {
        req << "Content-Type: application/json;charset=utf-8\r\n";
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n";
    req << body;
    std::string request = req.str();
    if (!write_all_fd(fd, request)) { close(fd); return false; }

    std::string raw; char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) raw.append(buf, buf + n);
        else if (n == 0) break;
        else { if (errno == EINTR) continue; close(fd); return false; }
    }
    close(fd);

    const std::string sep = "\r\n\r\n";
    auto p = raw.find(sep);
    if (p == std::string::npos) return false;
    std::istringstream hs(raw.substr(0, p));
    std::string ver; int status = 0; if (!(hs >> ver >> status)) return false;
    if (response_status) *response_status = status;
    if (response_body) *response_body = raw.substr(p + sep.size());
    return true;
}

bool JudgeManager::ExecuteRemotely(const JudgeJob& job, std::string* result_json) {
    int id = -1; oj::JudgeWorkerConfig cfg;
    if (!SelectWorker(&id, &cfg)) return false;
    OJ_LOG_INFO("JudgeManager: dispatch submission=" + std::to_string(job.submission_id) +
                " to worker " + WorkerTag(cfg));
    int status = 0; std::string resp;
    bool ok = SendHttpJson(cfg, "POST", cfg.judge_path, job.payload.dump(), &resp, &status);
    ReleaseWorkerLoad(id);
    if (!ok || status != 200) {
        OJ_LOG_WARN("JudgeManager: worker request failed " + WorkerTag(cfg) +
                    ", ok=" + std::string(ok ? "true" : "false") +
                    ", status=" + std::to_string(status) +
                    ", submission=" + std::to_string(job.submission_id));
        MarkOffline(id);
        return false;
    }
    if (result_json) *result_json = resp;
    OJ_LOG_INFO("JudgeManager: remote judge finished submission=" + std::to_string(job.submission_id) +
                ", worker=" + WorkerTag(cfg) +
                ", response_bytes=" + std::to_string(resp.size()));
    return true;
}

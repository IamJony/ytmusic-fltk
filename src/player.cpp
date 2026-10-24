#include "player.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <time.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

static const char* MP_CACHE_NORMAL = "4096";
static const char* MP_CACHE_MIN    = "5";

static double mono_clock() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

MplayerWrapper::MplayerWrapper() {}

MplayerWrapper::~MplayerWrapper() {
    running_ = false;
    if (reader_.joinable()) reader_.join();
    reap();
}

bool MplayerWrapper::init() {
    signal(SIGPIPE, SIG_IGN);
    return true;
}

void MplayerWrapper::set_event_handler(std::function<void(int)> handler) {
    std::lock_guard<std::mutex> g(cb_mtx_);
    on_event_ = std::move(handler);
}

void MplayerWrapper::notify(int event) {
    std::lock_guard<std::mutex> g(cb_mtx_);
    if (on_event_) on_event_(event);
}

static void write_all(int fd, const char* p, size_t n) {
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += w;
        n -= (size_t)w;
    }
}

void MplayerWrapper::send_cmd(const std::string& cmd) {
    if (in_fd_ < 0) return;
    std::string s = cmd + "\n";
    write_all(in_fd_, s.data(), s.size());
}

void MplayerWrapper::play(const std::string& url, bool audio_only) {
    stop();

    audio_only_ = audio_only;
    base_ = 0.0;
    pos_ = 0.0;
    length_ = 0.0;
    start_time_ = 0.0;
    paused_ = false;
    started_ = false;
    ended_ = false;
    end_notified_ = false;
    asked_.clear();
    last_pos_ = -1.0;
    last_t_ = 0.0;
    stagnant_ = 0;
    suppress_end_event_ = false;

    std::vector<std::string> a;
    a.push_back("mplayer");
    a.push_back("-slave");
    a.push_back("-quiet");
    a.push_back("-identify");
    a.push_back("-idle");
    a.push_back("-nomouseinput");
    a.push_back("-input");
    a.push_back("nodefault-bindings:conf=/dev/null");
    a.push_back("-noconsolecontrols");
    a.push_back("-vo"); a.push_back("null");
    a.push_back("-ao"); a.push_back("alsa,pulse,null");
    a.push_back("-cache"); a.push_back(MP_CACHE_NORMAL);
    a.push_back("-cache-min"); a.push_back(MP_CACHE_MIN);
    a.push_back("-volume"); a.push_back(std::to_string(volume_));
    a.push_back("-softvol");
    a.push_back("-correct-pts");
    if (audio_only_) a.push_back("-novideo");
    a.push_back("-osdlevel"); a.push_back("0");
    a.push_back(url);

    int pin[2], pout[2];
    if (pipe(pin) || pipe(pout)) return;

    pid_t p = fork();
    if (p == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        int r = nice(4); (void)r;
        dup2(pin[0], 0);
        dup2(pout[1], 1);
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) dup2(dn, 2);
        for (int fd = 3; fd < 256; fd++) close(fd);
        std::vector<char*> argv;
        for (auto& s : a) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        execvp("mplayer", argv.data());
        _exit(127);
    }

    close(pin[0]);
    close(pout[1]);

    pid_ = p;
    in_fd_ = pin[1];
    out_fd_ = pout[0];
    fcntl(out_fd_, F_SETFL, O_NONBLOCK);
    fcntl(in_fd_,  F_SETFD, FD_CLOEXEC);
    fcntl(out_fd_, F_SETFD, FD_CLOEXEC);

    session_id_++;
    reader_session_ = session_id_;
    running_ = true;
    int my_session = session_id_;
    reader_ = std::thread([this, my_session] { reader_loop(my_session); });
}

void MplayerWrapper::reap() {
    if (out_fd_ >= 0) { close(out_fd_); out_fd_ = -1; }
    if (in_fd_  >= 0) { close(in_fd_);  in_fd_  = -1; }
    if (pid_ > 0) {
        int st;
        for (int i = 0; i < 20 && waitpid(pid_, &st, WNOHANG) == 0; i++)
            usleep(50000);
        kill(-pid_, SIGKILL);
        if (waitpid(pid_, &st, WNOHANG) == 0)
            waitpid(pid_, &st, 0);
    }
    pid_ = -1;
}

void MplayerWrapper::stop() {
    suppress_end_event_ = true;
    running_ = false;
    if (pid_ > 0) {
        kill(-pid_, SIGKILL);
        kill(pid_, SIGKILL);
    }
    if (out_fd_ >= 0) { close(out_fd_); out_fd_ = -1; }
    if (in_fd_  >= 0) { close(in_fd_);  in_fd_  = -1; }
    if (reader_.joinable()) reader_.join();
    reap();
    started_ = false;
    paused_ = false;
    ended_ = false;
    end_notified_ = false;
    asked_.clear();
    pos_ = 0.0;
    length_ = 0.0;
    last_pos_ = -1.0;
    last_t_ = 0.0;
    stagnant_ = 0;
}

void MplayerWrapper::handle_line(const std::string& l, int my_session) {
    if (l.rfind("ANS_ERROR=", 0) == 0) {
        char q = asked_.empty() ? 0 : asked_.front();
        if (!asked_.empty()) asked_.pop_front();
        if (q == 'p' && started_ && !ended_ && !end_notified_) {
            ended_ = true;
            end_notified_ = true;
            if (!suppress_end_event_ && running_ && my_session == session_id_)
                notify(EVT_END_FILE);
        }
        return;
    }
    if ((l.rfind("ANS_TIME_POSITION=", 0) == 0 || l.rfind("ANS_LENGTH=", 0) == 0)
        && !asked_.empty()) {
        asked_.pop_front();
    }
    if (l.rfind("ANS_TIME_POSITION=", 0) == 0) {
        ended_ = false;
        double v = atof(l.c_str() + 18);
        std::lock_guard<std::mutex> g(mtx_);
        pos_ = v + base_;
        if (!started_ && pos_ > 0.0) started_ = true;
    } else if (l.rfind("ANS_LENGTH=", 0) == 0) {
        double v = atof(l.c_str() + 11);
        if (v > 0) {
            std::lock_guard<std::mutex> g(mtx_);
            length_ = v + base_;
        }
    } else if (l.rfind("ID_LENGTH=", 0) == 0) {
        double v = atof(l.c_str() + 10);
        if (v > 0) {
            std::lock_guard<std::mutex> g(mtx_);
            if (length_ <= 0) length_ = v + base_;
        }
    } else if (l.rfind("ID_START_TIME=", 0) == 0) {
        start_time_ = std::max(0.0, atof(l.c_str() + 14));
    } else if (l.rfind("Starting playback", 0) == 0) {
        started_ = true;
    } else if (l.rfind("ANS_pause=", 0) == 0) {
        paused_ = l.find("yes") != std::string::npos;
    }
}


void MplayerWrapper::reader_loop(int my_session) {
    char buf[4096];
    while (running_) {
        ssize_t n = read(out_fd_, buf, sizeof buf);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                poll_state(my_session);
                usleep(250000);
                continue;
            }
            break;
        }
        if (n == 0) break;
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (!linebuf_.empty()) {
                    handle_line(linebuf_, my_session);
                    linebuf_.clear();
                }
            } else if (linebuf_.size() < 4096) {
                linebuf_ += c;
            }
        }
        poll_state(my_session);
    }
    if (started_ && !end_notified_ && !suppress_end_event_ &&
        running_ && my_session == session_id_) {
        ended_ = true;
        end_notified_ = true;
        notify(EVT_END_FILE);
    }
}

void MplayerWrapper::poll_state(int my_session) {
    if (pid_ <= 0) return;
    if (asked_.size() >= 8) asked_.clear();
    if (!paused_) {
        send_cmd("pausing_keep_force get_time_pos");
        asked_.push_back('p');
        if (length_ <= 0) {
            send_cmd("pausing_keep_force get_time_length");
            asked_.push_back('l');
        }
    }

    double now = mono_clock();
    if (now - last_t_ >= 1.0) {
        last_t_ = now;
        double p = pos_;
        double len = length_;
        bool have_len = (len > 0.5);
        bool near_end = (have_len && p >= len - 3.0);
        bool stuck    = (last_pos_ >= 0 && (p - last_pos_) < 0.05 && (last_pos_ - p) < 0.05);

        if (started_ && !end_notified_ && have_len) {
            if (near_end && stuck) stagnant_++; else stagnant_ = 0;
            if (stagnant_ >= 2) {
                ended_ = true;
                end_notified_ = true;
                stagnant_ = 0;
                last_pos_ = -1.0;
                if (!suppress_end_event_ && running_ && my_session == session_id_)
                    notify(EVT_END_FILE);
            } else {
                last_pos_ = p;
            }
        } else {
            stagnant_ = 0;
            last_pos_ = p;
        }
    }
}

bool MplayerWrapper::is_running() {
    return pid_ > 0 && running_ && !ended_;
}

bool MplayerWrapper::is_paused() {
    return paused_;
}

void MplayerWrapper::toggle_pause() {
    if (pid_ <= 0) return;
    send_cmd("pause");
    paused_ = !paused_;
}

void MplayerWrapper::set_volume(int delta) {
    std::lock_guard<std::mutex> g(mtx_);
    volume_ += delta;
    if (volume_ < 0)   volume_ = 0;
    if (volume_ > 100) volume_ = 100;
    char b[48];
    snprintf(b, sizeof b, "pausing_keep_force volume %d 1", volume_);
    send_cmd(b);
}

void MplayerWrapper::set_volume_absolute(double volume) {
    std::lock_guard<std::mutex> g(mtx_);
    if (volume < 0)   volume = 0;
    if (volume > 100) volume = 100;
    volume_ = (int)volume;
    char b[48];
    snprintf(b, sizeof b, "pausing_keep_force volume %d 1", volume_);
    send_cmd(b);
}

void MplayerWrapper::seek(double seconds) {
    if (pid_ <= 0 || seconds < 0) return;
    char b[64];
    snprintf(b, sizeof b, "seek %.2f 2", seconds - base_ + start_time_);
    send_cmd(b);
    paused_ = false;
    std::lock_guard<std::mutex> g(mtx_);
    pos_ = seconds;
}

double MplayerWrapper::get_position() {
    std::lock_guard<std::mutex> g(mtx_);
    return pos_;
}

double MplayerWrapper::get_duration() {
    std::lock_guard<std::mutex> g(mtx_);
    return length_;
}

double MplayerWrapper::get_buffer() {
    return 0.0;
}

#ifndef SANSON_PLAYER_H
#define SANSON_PLAYER_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>

enum {
    EVT_END_FILE = 100,
};

class MplayerWrapper {
public:
    MplayerWrapper();
    ~MplayerWrapper();

    bool init();
    void play(const std::string& url, bool audio_only = true);
    void stop();
    void toggle_pause();
    void set_volume(int delta);
    void set_volume_absolute(double volume);
    bool is_paused();
    void seek(double seconds);
    double get_position();
    double get_duration();
    double get_buffer();
    bool is_running();
    void set_event_handler(std::function<void(int)> handler);

private:
    pid_t pid_ = -1;
    int in_fd_ = -1;
    int out_fd_ = -1;

    mutable std::mutex mtx_;
    std::string linebuf_;
    bool paused_ = false;
    bool started_ = false;
    bool ended_ = false;
    bool audio_only_ = true;
    double pos_ = 0.0;
    double length_ = 0.0;
    double base_ = 0.0;
    double start_time_ = 0.0;
    int volume_ = 80;

    std::deque<char> asked_;
    bool end_notified_ = false;
    bool suppress_end_event_ = false;
    int session_id_ = 0;
    int reader_session_ = 0;
    double last_pos_ = -1.0;
    double last_t_ = 0.0;
    int stagnant_ = 0;

    std::thread reader_;
    std::atomic<bool> running_{false};

    std::function<void(int)> on_event_;
    std::mutex cb_mtx_;

    void send_cmd(const std::string& cmd);
    void handle_line(const std::string& line, int my_session);
    void reader_loop(int my_session);
    void reap();
    void notify(int event);
    void poll_state(int my_session);
};

#endif

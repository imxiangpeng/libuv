#include <assert.h>
#include <curses.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "time_utils.h"

#define PERIOD_MS 50
#define SPEED_DEFAULT_AXIS_MAX 1  // 4 m/s

#define PANEL_TOP_ROWS 4

enum panel {
    PANEL_TOP = 0,
    PANEL_SPEED,
    PANEL_DISTANCE,
    _PANEL_MAX
};

#define TUI_DATA_WINDOW_MAX 200

struct tui_data_window {
    size_t size;
    int index;
    double max_value;
    double values[TUI_DATA_WINDOW_MAX];
};

static int tui_data_window_update(struct tui_data_window *w, double val) {
    w->values[w->index] = val;

    w->index = (w->index + 1) % TUI_DATA_WINDOW_MAX;  // circle buffer
    if (w->size != TUI_DATA_WINDOW_MAX) {
        w->size++;
    }
    if (fabs(val) > w->max_value) {
        w->max_value = fabs(val);
    }
    return 0;
}
static pthread_t _tui_tid = -1;
static volatile sig_atomic_t winch_received = 0;

// top: 4 line, max width
// speed: (height - 4) 2 / 5
// distance:  left
static WINDOW *_tui_panel[_PANEL_MAX] = {0};

static struct tui_data_window _tui_data_windows[2] = {{0}};
static double _accel_realtime = 0.0f;
static double _speed_realtime = 0.0f;
static double _distance_realtime = 0.0f;
static int _floor_realtime = 0.0f;

static void _signal_action(int signum, siginfo_t *siginfo, void *sigcontext) {
    (void)sigcontext;

    printf("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    if (SIGWINCH == signum) {
        winch_received = 1;
    }
}

void tui_draw_axes(WINDOW *win, int x, int y, int w, int h) {
    // draw x axis
    for (int i = x; i < x + w; i++) {
        mvwaddch(win, y, i, '-');
    }

    assert(y >= h);
    // draw y axis
    for (int i = y; i > y - h; i--) {
        mvwaddch(win, i, x, '|');
    }

    // draw (0,0)
    mvwaddch(win, y, x, '+');          // 原点
    mvwaddch(win, y, x + w - 1, '>');  // X 轴右端标记
    mvwaddch(win, y - h, x, '^');      // Y 轴上端标记

    wrefresh(win);
}

static void *tui_thread_routin(void *args) {
    int rows_max, cols_max, rows, cols;
    int layout_ready = 0;
    while (1) {
        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();
        if (layout_ready == 0 || winch_received == 1) {
            getmaxyx(stdscr, rows_max, cols_max);

            printf("rows_max:%d, cols_max:%d\n", rows_max, cols_max);
#if 1
            resize_term(rows_max, cols_max);

            if (!_tui_panel[PANEL_TOP]) {
                _tui_panel[PANEL_TOP] = newwin(PANEL_TOP_ROWS, cols_max, 0, 0);
            } else {
                wresize(_tui_panel[PANEL_TOP], PANEL_TOP_ROWS, cols_max);
            }

            rows = (rows_max - PANEL_TOP_ROWS) * 2 / 5;
            if (!_tui_panel[PANEL_SPEED]) {
                _tui_panel[PANEL_SPEED] = newwin(rows, cols_max, PANEL_TOP_ROWS, 0);
            } else {
                wresize(_tui_panel[PANEL_SPEED], rows, cols_max);
            }

            if (!_tui_panel[PANEL_DISTANCE]) {
                _tui_panel[PANEL_DISTANCE] = newwin(rows_max - PANEL_TOP_ROWS - rows, cols_max, PANEL_TOP_ROWS + rows, 0);
            } else {
                wresize(_tui_panel[PANEL_DISTANCE], rows_max - PANEL_TOP_ROWS - rows, cols_max);
                mvwin(_tui_panel[PANEL_DISTANCE], PANEL_TOP_ROWS + rows, 0);
            }

            clear();

            getmaxyx(_tui_panel[PANEL_SPEED], rows, cols);
            tui_draw_axes(_tui_panel[PANEL_SPEED], 0, rows - 1, cols, rows - 1);

            getmaxyx(_tui_panel[PANEL_DISTANCE], rows, cols);
            tui_draw_axes(_tui_panel[PANEL_DISTANCE], 0, rows - 1, cols, rows - 1);

            wrefresh(_tui_panel[PANEL_TOP]);
            wrefresh(_tui_panel[PANEL_SPEED]);
            wrefresh(_tui_panel[PANEL_DISTANCE]);
#endif
            winch_received = 0;
            layout_ready = 1;
        }

        // draw top panel
        werase(_tui_panel[PANEL_TOP]);
        mvwprintw(_tui_panel[PANEL_TOP], 0, 2, "Acceleration: %.3f m/s^2", _accel_realtime);
        mvwprintw(_tui_panel[PANEL_TOP], 1, 2, "Speed: %.3f m/s", _speed_realtime);
        mvwprintw(_tui_panel[PANEL_TOP], 2, 2, "Distance: %.3f m", _distance_realtime);
        mvwprintw(_tui_panel[PANEL_TOP], 3, 2, "Floor: %d", _floor_realtime);
        wrefresh(_tui_panel[PANEL_TOP]);

        tui_data_window_update(&_tui_data_windows[0], _speed_realtime);
        tui_data_window_update(&_tui_data_windows[1], _distance_realtime);

        // draw speed panel
        {
            int i = 0, x = 0;
            struct tui_data_window *w = &_tui_data_windows[0];

            getmaxyx(_tui_panel[PANEL_SPEED], rows, cols);

            int available_x = cols - 1;  // remove > label
            int available_y = rows - 1;  // remove ^ label
            int max_label = ceil(fabs(w->max_value));
            if (max_label < SPEED_DEFAULT_AXIS_MAX) {
                max_label = SPEED_DEFAULT_AXIS_MAX;
            }

            // draw axis label (max speed)
            werase(_tui_panel[PANEL_SPEED]);
            tui_draw_axes(_tui_panel[PANEL_SPEED], 0, rows - 1, cols, rows - 1);

            mvwprintw(_tui_panel[PANEL_SPEED], 0, 2, "%.2f m/s", max_label * 1.0);

            // draw from end to begin
#if 1
            x = cols - 1;
            for (i = w->index - 1; i >= 0 && x >= 0; i--) {
                int y = round(available_y * (1 - w->values[i] / max_label));
                mvwaddch(_tui_panel[PANEL_SPEED], y, x, '*');
                x--;
            }

            if (w->size == TUI_DATA_WINDOW_MAX) {
                for (i = w->size - 1; i >= w->index && x >= 0; i--) {
                    int y = available_y * (1 - w->values[i] / max_label);
                    mvwaddch(_tui_panel[PANEL_SPEED], y, x, '*');
                    x--;
                }
            }
#else
            x = 0;
            if (w->size == TUI_DATA_WINDOW_MAX) {
                for (i = w->index; i < TUI_DATA_WINDOW_MAX; i++) {
                    int y = ceil(available_y * (1 - w->values[i] / max_label));
                    mvwaddch(_tui_panel[PANEL_SPEED], y, x, '*');
                    x++;
                }
            }
            for (i = 0; i < w->index; i++) {
                int y = ceil(available_y * (1 - w->values[i] / max_label));
                mvwaddch(_tui_panel[PANEL_SPEED], y, x, '*');
                x++;
            }
#endif
            wrefresh(_tui_panel[PANEL_SPEED]);
        }

        // draw distance panel
        {
            int i = 0, x = 0;
            struct tui_data_window *w = &_tui_data_windows[1];

            getmaxyx(_tui_panel[PANEL_DISTANCE], rows, cols);

            int available_x = cols - 1;  // remove > label
            int available_y = rows - 1;  // remove ^ label
            int max_label = ceil(fabs(w->max_value));

            // draw axis label (max speed)
            werase(_tui_panel[PANEL_DISTANCE]);
            tui_draw_axes(_tui_panel[PANEL_DISTANCE], 0, rows - 1, cols, rows - 1);

            mvwprintw(_tui_panel[PANEL_DISTANCE], 0, 2, "%.2f m", max_label * 1.0);

            // draw from end to begin
            x = cols - 1;
            for (i = w->index - 1; i >= 0 && x >= 0; i--) {
                int y = round(available_y * (1 - w->values[i] / max_label));
                mvwaddch(_tui_panel[PANEL_DISTANCE], y, x, '*');
                x--;
            }

            if (w->size == TUI_DATA_WINDOW_MAX) {
                for (i = w->size - 1; i >= w->index && x >= 0; i--) {
                    int y = available_y * (1 - w->values[i] / max_label);
                    mvwaddch(_tui_panel[PANEL_DISTANCE], y, x, '*');
                    x--;
                }
            }

            wrefresh(_tui_panel[PANEL_DISTANCE]);
        }

        usleep(1000 * PERIOD_MS);
    };
    return NULL;
}

static void tui_thread_start(void) {
    int ret = 0;
    pthread_attr_t attr;

    pthread_attr_init(&attr);

    ret = pthread_create(&_tui_tid, &attr, tui_thread_routin, NULL);
    if (0 != ret) {
        printf("%s(%d): failed to pthread_create\n", __FUNCTION__, __LINE__);
        return;
    }
    pthread_attr_destroy(&attr);
}

static void _observer_update(enum core_sensor sensor, void *data) {
    struct live_stat *stat = (struct live_stat *)data;
    _accel_realtime = stat->accel;
    _speed_realtime = stat->speed;
    _distance_realtime = stat->distance;
    _floor_realtime = stat->floor;
    // printf("speed : %f\n", _speed_realtime);
    // tui_data_window_update(&_tui_data_windows[0], _speed_realtime);
    // tui_data_window_update(&_tui_data_windows[1], _distance_realtime);
}

static struct core_observer _tui_core_observer = {
    .update = _observer_update};

int tui_init() {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGWINCH, &action, NULL);

    // init ncurses
    initscr();
    raw();
    cbreak();
    noecho();
    // nodelay(stdscr, TRUE);
    // keypad(stdscr, TRUE);
    curs_set(0);
    timeout(0);

    keypad(stdscr, TRUE);

    core_register_observer(SENSOR_ACCELERATION, &_tui_core_observer);

    tui_thread_start();
    return 0;
}

int tui_deinit() {
    pthread_join(_tui_tid, NULL);
    endwin();
    return 0;
}

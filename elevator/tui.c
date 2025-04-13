#include <assert.h>
#include <curses.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "time_utils.h"
#include "core.h"

#define PERIOD_MS 500

#define PANEL_TOP_ROWS 4

enum panel {
    PANEL_TOP = 0,
    PANEL_SPEED,
    PANEL_DISTANCE,
    _PANEL_MAX
};

static pthread_t _tui_tid = -1;
static volatile sig_atomic_t winch_received = 0;

// top: 4 line, max width
// speed: (height - 4) 2 / 5
// distance:  left
static WINDOW* _tui_panel[_PANEL_MAX] = {0};
static void _signal_action(int signum, siginfo_t *siginfo, void *sigcontext) {
    (void)sigcontext;

    printf("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    if (SIGWINCH== signum) {
        winch_received = 1;
    }
}

void tui_draw_axes(WINDOW *win, int x, int y, int w, int h) {
    // draw x axis
    for (int i = x; i < x + w; i++) {
        mvwaddch(win, y , i, '-');
    }

    assert(y >= h);
    // draw y axis
    for (int i = y; i > y - h; i--) {
        mvwaddch(win, i, x, '|');
    }

    // draw (0,0)
    mvwaddch(win, y, x, '+');  // 原点
    mvwaddch(win, y, x + w - 1, '>');  // X 轴右端标记
    mvwaddch(win, y - h, x, '^');  // Y 轴上端标记

    wrefresh(win);
}

static void *tui_thread_routin(void *args) {

    int layout_ready = 0;
    while(1) {

        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();
        if (layout_ready == 0 || winch_received == 1) {
            int rows_max, cols_max, rows, cols;
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
            }  else {
                wresize(_tui_panel[PANEL_DISTANCE], rows_max - PANEL_TOP_ROWS - rows, cols_max);
                mvwin(_tui_panel[PANEL_DISTANCE], PANEL_TOP_ROWS + rows, 0);
            }
          
            clear();
            
            
            getmaxyx(_tui_panel[PANEL_TOP], rows, cols);
            mvwprintw(_tui_panel[PANEL_TOP], 0, 0, "Window TOP: %dx%d -> %d x %d", rows /*PANEL_TOP_ROWS*/, cols /*cols_max*/, rows_max, cols_max);
            getmaxyx(_tui_panel[PANEL_SPEED], rows, cols);
            mvwprintw(_tui_panel[PANEL_SPEED], 0, 0, "Window SPEED: %dx%d", rows, cols);
            getmaxyx(_tui_panel[PANEL_DISTANCE], rows, cols);
            mvwprintw(_tui_panel[PANEL_DISTANCE], 0, 0, "Window DISTANCE: %dx%d", rows, cols);
            

            getmaxyx(_tui_panel[PANEL_SPEED], rows, cols);
            tui_draw_axes(_tui_panel[PANEL_SPEED], 0, rows -1 , cols, rows - 1);
            
            getmaxyx(_tui_panel[PANEL_DISTANCE], rows, cols);
            tui_draw_axes(_tui_panel[PANEL_DISTANCE], 0, rows -1 , cols, rows - 1);

            wrefresh(_tui_panel[PANEL_TOP]);
            wrefresh(_tui_panel[PANEL_SPEED]);
            wrefresh(_tui_panel[PANEL_DISTANCE]);
#endif            
            winch_received = 0;
            layout_ready = 1;
        }
        
        usleep(1000 * PERIOD_MS)
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
    //nodelay(stdscr, TRUE);
    //keypad(stdscr, TRUE);
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
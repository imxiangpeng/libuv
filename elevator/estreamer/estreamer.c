
// mxp, 20250618, elevator stream media record files
// 1. to ftp address
// 2. to rtmp address

#define _GNU_SOURCE
// #define _XOPEN_SOURCE 600

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wordexp.h>

#include "file_util.h"
#include "hr_buffer.h"
#include "hr_log.h"

#ifndef IPC_MEDIA_RECORD_DIR
// #define IPC_MEDIA_RECORD_DIR "/media/mmcblk0p1"
#endif
#define IPC_MEDIA_RECORD_DIR "/home/alex/workspace/workspace/libuv/libuv/build/media"
#define ESTREAMER_TASK_DIR IPC_MEDIA_RECORD_DIR "/estreamer"

// 2025-05-26_18-22-00_duration.mp4
#define MEDIA_RECORD_DATE_STRING_FORMAT "%Y-%m-%d_%H-%M-%S"

struct record {
    /*uint64_t*/ time_t timestamp;  // utc use timegm not mktime
    int duration;
    char name[256];
    int32_t clip_start;
    int32_t clip_end;
};

enum task_state {
    TASK_STATE_INITIALIZE = 0,
    TASK_STATE_TRAVERSE,  // have traversed media record list & concat file is generated when needed
    TASK_STATE_CONCAT,    // running ffmpeg to generate new video file
    TASK_STATE_UPLOAD,    // do upload, maybe you should use continue-at
};

static char _task_path[512] = {0};
static int _exit_request = 0;

static time_t media_record_date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, MEDIA_RECORD_DATE_STRING_FORMAT, &tm) == NULL) {
        return 0;
    }

    return timegm(&tm);  // do not care timezone
}

static int compare_record_by_timestamp(const void* a, const void* b) {
    const struct record* ra = (const struct record*)a;
    const struct record* rb = (const struct record*)b;
    return (int)(ra->timestamp - rb->timestamp);
}

static int traverse_media_record_list(struct hrbuffer* lists, uint64_t begin, uint64_t end, int* completed) {
    char* ptr = NULL;

    struct record media;
    struct dirent* entry = NULL;

    struct stat sb;
    size_t count = 0;

    if (!completed) {
        return -1;
    }

    DIR* dir = opendir(IPC_MEDIA_RECORD_DIR);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        char* path = NULL;
        asprintf(&path, IPC_MEDIA_RECORD_DIR "/%s", entry->d_name);

        if (!path) {
            continue;
        }

        if (stat(path, &sb) != 0 || !S_ISREG(sb.st_mode)) {
            free(path);
            continue;
        }
        free(path);

        if ((ptr = strstr(entry->d_name, ".mp4"))) {
            int duration = 0;
            uint64_t ts = media_record_date_format_string_to_seconds(entry->d_name);
            if (ts == 0) {
                continue;
            }

            if (sscanf(entry->d_name, "%*d-%*d-%*d_%*d-%*d-%*d_%d", &duration) != 1) {
                continue;
            }

            // ts + duration > begin && ts < end
            if (ts + duration > begin && ts < end) {
                count++;
                memset((void*)&media, 0, sizeof(media));
                media.timestamp = ts;
                media.duration = duration;
                snprintf(media.name, sizeof(media.name), "%s", entry->d_name);

                media.clip_start = 0;
                media.clip_end = 0;

                if (ts < begin) {
                    media.clip_start = begin - ts;
                }

                if (ts + duration > end) {
                    media.clip_end = end - ts;
                }
                printf("==>count:%ld begin:%ld, end:%ld, time:%ld, name:%s, clip:%d-%d\n", count, begin, end, ts, media.name, media.clip_start, media.clip_end);
                // hrbuffer_append(&record_lists, (void*)&media, sizeof(struct record));
                hrbuffer_append(lists, (void*)&media, sizeof(struct record));
            }
        }
    }
    closedir(dir);

    if (count != 0) {
        // qsort(record_lists.data, count, sizeof(struct record), compare_record_by_timestamp);
        qsort(lists->data, count, sizeof(struct record), compare_record_by_timestamp);

        // struct record* r = (struct record*)record_lists.data;
        struct record* r = (struct record*)lists->data;

        for (size_t i = 0; i < count; i++) {
            printf("%ld -> %ld : %s, clip:[%d,%d]\n", i, r[i].timestamp, r[i].name, r[i].clip_start, r[i].clip_end);
        }

        if ((uint64_t)r[count - 1].timestamp + r[count - 1].duration < end) {
            printf("maybe not finished, we should wait ...\n");
            *completed = 0;
        } else {
            *completed = 1;
        }
    }

    // hrbuffer_free(&record_lists);

    return count;
}

static int64_t date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, "%Y%m%d%H%M%S", &tm) == NULL) {
        return 0;
    }

    // return mktime(&tm);
    return timegm(&tm);  // do not care timezone
}

static void task_dir_sync() {
    int fd = open(_task_path, O_DIRECTORY | O_RDONLY);
    if (fd > 0) {
        fsync(fd);
        close(fd);
    }
}
static int write_task_node(const char* node, int val) {
    if (!node) {
        return -1;
    }
    FILE* fp = fopen(node, "w");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "%d", val);

    fflush(fp);

    // fdatasync(fileno(fp));
    fsync(fileno(fp));

    fclose(fp);

    task_dir_sync();
    sync();  // make sure again
    printf("%s -> %d\n", node, val);

    return 0;
}

static int read_task_node(const char* node, int* val) {
    if (!node || !val) {
        return -1;
    }
    FILE* fp = fopen(node, "r");
    if (!fp) {
        return -1;
    }

    if (1 != fscanf(fp, "%d", val)) {
        fclose(fp);
        return -1;
    }

    fclose(fp);

    return 0;
}

static int write_task_node_string(const char* node, const char* val) {
    if (!node || !val) {
        return -1;
    }
    FILE* fp = fopen(node, "w");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "%s", val);

    fflush(fp);

    // fdatasync(fileno(fp));
    fsync(fileno(fp));

    fclose(fp);

    task_dir_sync();
    printf("%s -> %s\n", node, val);
    sync();  // make sure again

    return 0;
}

static int read_task_node_string(const char* node, char* val, int length) {
    if (!node || !val || length <= 0) {
        return -1;
    }
    FILE* fp = fopen(node, "r");
    if (!fp) {
        return -1;
    }

    if (!fgets(val, length, fp)) {
        printf("can not read valid node:%s\n", node);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    printf("node %s:%s\n", node, val);

    return 0;
}

static void release_task() {
    printf("release task: %s\n", _task_path);
    if (_task_path[0] == '\0') {
        return;
    }

    chdir("/");

    futil_unlink(_task_path);
    sync();
}

static int do_cmd(const char* cmd) {
    int pipefd[2] = {-1, -1};
    pid_t pid;
    int status;
    wordexp_t p;

    if (!cmd || cmd[0] == '\0') {
        return -1;
    }

    if (wordexp(cmd, &p, 0) != 0) {
        return -1;
    }

    // for (size_t i = 0; i < p.we_wordc; i++) {
    //     printf("argv[%zu] = %s\n", i, p.we_wordv[i]);
    // }

    if (pipe(pipefd) < 0) {
        wordfree(&p);
        return -1;
    }

    pid = fork();
    if (pid == 0) {
        // child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(p.we_wordv[0], p.we_wordv);
        _exit(127);
    }

    // parent
    close(pipefd[1]);
    wordfree(&p);

    char buf[1024] = {0};
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        write(STDOUT_FILENO, buf, n);
    }
    close(pipefd[0]);

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        // printf("status:%d\n", WEXITSTATUS(status));
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        // printf("signal:%d\n", 128 + WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return 0;
}

static void _signal_action(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)siginfo;
    (void)sigcontext;

    HR_LOGD("%s(%d): ....estreamer....signum:%d\n", __FUNCTION__, __LINE__, signum);
    printf("%s(%d): ...estreamer.....signum:%d\n", __FUNCTION__, __LINE__, signum);

    if (SIGTERM == signum) {
        _exit_request = 1;
        release_task();
        kill(getpgrp(), SIGKILL);
    }
}
// mxp, 20250926 本来计划通过捕获信号来正常释放资源，
// 但是我们发现当 estreamer 设置了 setpgid(0, 0) 变成独立进程组之后，
// kill -15 pid 就失效了，无法捕获，但是可以收到 SIGKILL
// 所以，我们在 hqliftd 中目前只能通过 SIGKILL 来结束未完成的事项
// 那么出现了一个问题，如果用户两次请求同一个回放，那么在前面回放尚未结束时，
// 我们会将其 KILL， 因为 estreamer 捕获不到，所以无法清理资源，
// 导致其工作的目录还存在，按我们之前的逻辑下次再请求相同资源的时候，会直接释放
// 现在我们简单修改以下，支持参数 -b 表示开机启动，我们仅在开机启动的时候来清理
// 这里非永久任务

// 为了支持断电保存和断点续传，我们将任务保存到独立的文件夹里面，
// 每个文件夹对应一个任务，文件夹采用 _ 连接起始时间戳来命名
// 具体任务信息以文件形式保存在文件夹内部。
// 例如:
// - persist 是否持久任务，断电后仍然继续
// - url 对应需要上传的地址
// - task 在已经检索到录制文件后，我们会生成任务脚本
// - state 任务脚本执行过程中状态
// 如果 estreamer 刚启动，那么目录以及 url 会第一时间创建，
// 之后, estreamer 会检索指定范围内录制文件，如果检索成功，
// 那么会生成对应 cmd 脚本，同时更新 state 状态
// 之后的工作交给 cmd 脚本来完成
// 在完成后更新下一步 cmd 脚本以及 state 状态, 直到整个任务完成

// 问题：

// stream to
// 1. ftp
// curl --ftp-pasv --retry 5 --retry-delay 5 --retry-max-time 60
// -T output.mp4 ftp://user:password@ftp.example.com/videos/output.mp4
// 2. rtmp
// 3. local

// estreamer begin_timestamp end_timestamp url
// estreamer 20250617060012 20250617060700 ftp://inspur:inspur88%2A@ftp.hqszjs.com:2100/event_files/GD500103000172/20250617_060012.mp4
// allowing using -s as first parameter to ensure the task completed even after a reboot or power off
// elevator/estreamer/estreamer -s 20250905120000 20250905121500 ftp://ftp.hqszjs.com:2100/event_files/GD500103000171/20250617_060012.mp4
// resume task will complete this tasks when boot complete: S99estreamer-persist-task
// it will pass only begin and end timestamp:
// elevator/estreamer/estreamer 20250905120000 20250905121500
// estreamer_resume() {
//     # load ftp user & passwd
//     . /etc/hqliftd/hqliftd.conf
//     export FTP_USERNAME
//     export FTP_PASSWORD

//     for dir in "$MNT_POINT"/estreamer/*; do
//         [ -d "$dir" ] || continue
//         base=$(basename "$dir")

//         if echo "$base" | grep -qE '^[0-9]{14}_[0-9]{14}$'; then
//             start_time="${base%%_*}"
//             end_time="${base##*_}"

//             logger -t $TAG "$base -> $start_time $end_time"

//             estreamer "$start_time" "$end_time"
//         else
//             logger -t $TAG "Skipping invalid directory $base"
//         fi
//     done
// }
int main(int argc, const char** argv) {
    (void)argc;
    (void)argv;

    pid_t pid = -1;
    int status = 0;
    struct sigaction action;
    int boot = 0;
    int persist = 0;
    char cmd[LINE_MAX] = {0};
    struct stat sb;

    int state = TASK_STATE_INITIALIZE;

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);

    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;

    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, NULL);

    action.sa_flags = SA_SIGINFO;
    sigaction(SIGTERM, &action, NULL);

    if (argc < 3) {
        return -1;
    }

    // save
    if (0 == strcmp(argv[1], "-s")) {
        persist = 1;
        argv++;
        argc--;
    }

    // boot up
    if (0 == strcmp(argv[1], "-b")) {
        boot = 1;
        argv++;
        argc--;
    }

    for (int i = 0; i < argc; i++) {
        printf("%d: %s\n", i, argv[i]);
    }

    // redirect null to input
    int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd > 0) {
        dup2(null_fd, 0);
        close(null_fd);
        // fcntl(0, F_SETFD, fcntl(0, F_GETFD) | ~FD_CLOEXEC);
    }

    // kill all children process
    setpgid(0, 0);

    const char* begin = argv[1];
    const char* end = argv[2];
    char url[1024] = {0};
    // user may not pass -s flag & url when resume from shutdown
    if (argc == 4) {
        snprintf(url, sizeof(url), "%s", argv[3]);
    }

    // const char* begin = "2025-06-18 11:31:22";
    // const char* end = "2025-06-18 11:45:22";
    // discontinue
    // const char* begin = "20250612150330";
    // const char* end = "2025-06-12 15:18:20";
    // const char* begin = "2025-06-12 15:03:25";
    // const char* end = "20250612150345";
    // const char* end = "2025-06-12 15:03:40";

    // const char* begin = "2025-06-11 17:38:24";
    // const char* end = "2025-06-11 18:04:16";

    // const char* begin = "2025-06-17 06:04:19";
    // const char* end = "2025-06-17 06:06:19";
    if (!begin || !end /*|| !url*/) {
        return -1;
    }

    printf("begin:%s\n", begin);
    printf("end:%s\n", end);
    state = TASK_STATE_INITIALIZE;

    if (-1 == mkdir(ESTREAMER_TASK_DIR, 0755)) {
        if (errno != EEXIST) {
            printf("can not prepare estreamer work directory\n");
            return -1;
        }
    }

    snprintf(_task_path, sizeof(_task_path), ESTREAMER_TASK_DIR /*IPC_MEDIA_RECORD_DIR*/ "/%s_%s", begin, end);
    // check task already exists ?
    if (-1 == lstat(_task_path, &sb)) {
        if (boot == 1) {
            return -1;
        }
        // create new task
        if (errno == ENOENT) {
            if (url[0] == '\0') {
                return -1;
            }
            int r = mkdir(_task_path, 0755);
            if (r == -1) {
                if (errno != EEXIST) {
                    printf("create dir:%s failed!\n", _task_path);
                    return -1;
                }
                printf("mkdir failed maybe exists already\n");
            }

            chdir(_task_path);

            write_task_node_string("url", url);
            write_task_node("persist", persist);
            state = TASK_STATE_TRAVERSE;
            write_task_node("state", state);
        } else {
            printf("task init failed!\n");
            return -1;
        }
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            printf("error %s is not directory\n", _task_path);
            return -1;
        }

        printf("this is existing task ...\n");

        chdir(_task_path);

        read_task_node("persist", &persist);
        read_task_node("state", &state);

        read_task_node_string("url", url, sizeof(url));
        read_task_node_string("cmd", cmd, sizeof(cmd));

        printf("persist:%d, state:%d, cmd:%s\n", persist, state, cmd);
        // if rtmp live destory task
        if (persist == 0 && boot == 1) {
            printf("no need process none persist task, delete it\n");
            release_task();
            return 0;
        }
    }

    if (state == TASK_STATE_TRAVERSE) {
        printf("do traverse ...\n");
        FILE* concat_fp = NULL;
        struct hrbuffer list;

        int completed = 0;
        int count = 0;
        int l = 0;
        int64_t b = date_format_string_to_seconds(begin);
        int64_t e = date_format_string_to_seconds(end);

        memset((void*)&list, 0, sizeof(list));

        if (hrbuffer_alloc(&list, sizeof(struct record) * 10) < 0) {
            // release task directory
            release_task();
            return 0;
        }

        // wait 10min
        while (l++ < 60) {
            hrbuffer_reset(&list);
            count = traverse_media_record_list(&list, b, e, &completed);

            if (completed == 1) {
                break;
            }

            printf("%s(%d): traverse count:%d, completed:%d, l:%d\n", __FUNCTION__, __LINE__, count, completed, l);
            usleep(10 * 1000 * 1000);
        }

        if (count == 0) {
            hrbuffer_free(&list);
            // no valid record files, release task directory
            release_task();
            return -1;
        }

        if (count == 1) {
            struct record* r = (struct record*)list.data;
            if (r->clip_start == 0 && r->clip_end == 0) {
                state = TASK_STATE_UPLOAD;

                printf("do traverse generate upload cmd...\n");
                if (0 == strncmp(url, "ftp://", strlen("ftp://"))) {
                    const char* user = getenv("FTP_USERNAME");
                    const char* passwd = getenv("FTP_PASSWORD");
                    snprintf(cmd, sizeof(cmd),
                             "curl -C - -s --retry 5 --retry-delay 5 --retry-max-time 60 --ftp-create-dirs -T %s/%s %s -u '%s:%s'",
                             IPC_MEDIA_RECORD_DIR, r->name, url, user ? user : "anonymous", passwd ? passwd : "");
                } else if (0 == strncmp(url, "rtmp://", strlen("rtmp://"))) {
                    snprintf(cmd, sizeof(cmd), "ffmpeg -d -loglevel quiet -y -re -safe 0 -i %s/%s -c copy -f flv %s", IPC_MEDIA_RECORD_DIR, r->name, url);
                }

                write_task_node_string("cmd", cmd);
                write_task_node("state", state);

                hrbuffer_free(&list);
                goto state_upload;
            }
        }

        printf("do traverse generate concat command ...\n");
        concat_fp = fopen("concat.txt", "w+");
        if (!concat_fp) {
            hrbuffer_free(&list);
            printf("can not open file :concat.txt\n");
            release_task();
            return -1;
        }

        for (int i = 0; i < count; i++) {
            struct record* r = (struct record*)list.data;
            printf("%d -> %ld : %s, clip:[%d,%d]\n", i, r[i].timestamp, r[i].name, r[i].clip_start, r[i].clip_end);

            fprintf(concat_fp, "file '%s/%s'\n", IPC_MEDIA_RECORD_DIR, r[i].name);

            if (r[i].clip_start != 0) {
                fprintf(concat_fp, "inpoint %d\n", r[i].clip_start);
            }

            if (r[i].clip_end != 0) {
                fprintf(concat_fp, "outpoint %d\n", r[i].clip_end);
            }
        }

        fflush(concat_fp);
        fsync(fileno(concat_fp));
        fclose(concat_fp);
        hrbuffer_free(&list);

        // it's live directly upstream
        if (0 == strncmp(url, "rtmp://", strlen("rtmp://"))) {
            snprintf(cmd, sizeof(cmd), "ffmpeg -d -loglevel quiet -y -re -f concat -safe 0 -i concat.txt -c copy -f flv %s", url);
            write_task_node_string("cmd", cmd);
            state = TASK_STATE_UPLOAD;
            write_task_node("state", state);
            goto state_upload;
        }

        snprintf(cmd, sizeof(cmd), "ffmpeg -d -loglevel quiet -y -f concat -safe 0 -i concat.txt -c copy -f mp4 concat.mp4");
        write_task_node_string("cmd", cmd);

        state = TASK_STATE_CONCAT;
        write_task_node("state", state);
        goto state_concat;
    }

state_concat:

    if (state == TASK_STATE_CONCAT) {
        printf("do concat ...\n");
        if (cmd[0] == '\0') {
            printf("none cmd:%s\n", cmd);
            // release resource
            goto release;
        }
        // printf("cmd:%s\n", cmd);
        // system(cmd);
        int ret = do_cmd(cmd);
        // printf("ret:%d, cmd: %s\n", ret, cmd);
        if (ret == 0) {
            sync();
        }
        memset((void*)cmd, 0, sizeof(cmd));

        if (0 == strncmp(url, "ftp://", strlen("ftp://"))) {
            const char* user = getenv("FTP_USERNAME");
            const char* passwd = getenv("FTP_PASSWORD");
            snprintf(cmd, sizeof(cmd),
                     "curl -C - -s --retry 5 --retry-delay 5 --retry-max-time 60 --ftp-create-dirs -T concat.mp4 %s -u '%s:%s'",
                     url, user, passwd);
            write_task_node_string("cmd", cmd);
        }

        state = TASK_STATE_UPLOAD;
        write_task_node("state", state);
        goto state_upload;
    }

state_upload:
    if (state == TASK_STATE_UPLOAD) {
        int retries = 5;
        if (cmd[0] == '\0') {
            // release
            goto release;
        }

        while (retries-- > 0) {
            int ret = do_cmd(cmd);
            printf("upload do cmd ret:%d, cmd: %s\n", ret, "***" /*cmd*/);
            if (ret == 0) {
                break;
            }

            // rtmp using ffmpeg streaming
            if (0 == strncmp(cmd, "ffmpeg", 6)) {
                printf("ignore ffmpeg upload result!\n");
                break;
            }

            usleep(1000 * 1000 * 60);  // retry after 1 min
        }

        memset((void*)cmd, 0, sizeof(cmd));
    }

release:
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("estreamer child pid:%d, status:%d\n", pid, status);
    }

    printf("release task\n");
    release_task();

    return 0;
}
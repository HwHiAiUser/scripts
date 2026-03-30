/*
 * fanctl - Fan control utility for Orange Pi AI Pro 20T (Ascend 310B)
 *
 * Controls fan speed via /dev/pwm ioctl interface provided by pwm.ko
 *
 * Usage:
 *   fanctl get              - Get current duty ratio and fan speed
 *   fanctl set <ratio>      - Set duty ratio (0-100)
 *   fanctl speed            - Get fan speed (RPM)
 *   fanctl auto             - Enable auto temperature-based adjustment
 *   fanctl manual           - Disable auto adjustment (manual mode)
 *   fanctl mode             - Get current adjustment mode
 *   fanctl monitor          - Continuously monitor fan speed
 *
 * Build:  aarch64-linux-gnu-gcc -O2 -o fanctl fanctl.c
 * Deploy: scp fanctl orange:/usr/local/bin/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

/* ioctl definitions matching pwm_drv.h */
#define PWM_IOC_MAGIC 'p'
#define PWM_CMD_GET_DUTY_RATIO             _IOR(PWM_IOC_MAGIC, 0, int)
#define PWM_CMD_SET_DUTY_RATIO             _IOW(PWM_IOC_MAGIC, 1, int)
#define PWM_CMD_GET_DUTY_RATIO_ADJUST_MODE _IOR(PWM_IOC_MAGIC, 2, int)
#define PWM_CMD_SET_DUTY_RATIO_ADJUST_MODE _IOW(PWM_IOC_MAGIC, 3, int)
#define PWM_CMD_GET_FAN_SPEED              _IOW(PWM_IOC_MAGIC, 4, int)

#define PWM_DEV "/dev/pwm"
#define PWM_CHANNEL 2  /* FAN_PWM_CHANNEL in pwm_drv.h */
#define PWM_MAX_RATIO 100

typedef struct {
    unsigned int channel_num;
    unsigned int ratio;
    unsigned int speed;
} PWM_INFO;

enum pwm_mode {
    PWM_MANUAL = 0,
    PWM_AUTO = 1,
};

static int pwm_open(void)
{
    int fd = open(PWM_DEV, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", PWM_DEV, strerror(errno));
        if (errno == EACCES)
            fprintf(stderr, "Try running with sudo\n");
        if (errno == ENOENT)
            fprintf(stderr, "Is pwm.ko loaded? Try: modprobe pwm\n");
    }
    return fd;
}

static int cmd_get(int channel)
{
    int fd = pwm_open();
    if (fd < 0) return 1;

    PWM_INFO info = { .channel_num = channel };
    if (ioctl(fd, PWM_CMD_GET_DUTY_RATIO, &info) < 0) {
        fprintf(stderr, "ioctl GET_DUTY_RATIO failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);

    printf("Channel: %u\n", info.channel_num);
    printf("Duty:    %u%%\n", info.ratio);
    printf("Speed:   %u RPM\n", info.speed);
    return 0;
}

static int cmd_set(int channel, unsigned int ratio)
{
    if (ratio > PWM_MAX_RATIO) {
        fprintf(stderr, "Ratio must be 0-%d\n", PWM_MAX_RATIO);
        return 1;
    }

    int fd = pwm_open();
    if (fd < 0) return 1;

    PWM_INFO info = { .channel_num = channel, .ratio = ratio };
    if (ioctl(fd, PWM_CMD_SET_DUTY_RATIO, &info) < 0) {
        fprintf(stderr, "ioctl SET_DUTY_RATIO failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);

    printf("Set channel %u duty ratio to %u%%\n", channel, ratio);
    return 0;
}

static int cmd_speed(int channel)
{
    int fd = pwm_open();
    if (fd < 0) return 1;

    PWM_INFO info = { .channel_num = channel };
    if (ioctl(fd, PWM_CMD_GET_FAN_SPEED, &info) < 0) {
        fprintf(stderr, "ioctl GET_FAN_SPEED failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);

    printf("%u\n", info.speed);
    return 0;
}

static int cmd_mode_get(void)
{
    int fd = pwm_open();
    if (fd < 0) return 1;

    int mode = -1;
    if (ioctl(fd, PWM_CMD_GET_DUTY_RATIO_ADJUST_MODE, &mode) < 0) {
        fprintf(stderr, "ioctl GET_ADJUST_MODE failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);

    printf("Mode: %s (%d)\n", mode == PWM_AUTO ? "auto" : "manual", mode);
    return 0;
}

static int cmd_mode_set(int mode)
{
    int fd = pwm_open();
    if (fd < 0) return 1;

    if (ioctl(fd, PWM_CMD_SET_DUTY_RATIO_ADJUST_MODE, &mode) < 0) {
        fprintf(stderr, "ioctl SET_ADJUST_MODE failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);

    printf("Set mode: %s\n", mode == PWM_AUTO ? "auto" : "manual");
    return 0;
}

static int cmd_monitor(int channel, int interval)
{
    int fd = pwm_open();
    if (fd < 0) return 1;

    printf("Monitoring fan (channel %d), Ctrl+C to stop...\n", channel);
    printf("%-12s %-8s %-10s\n", "Time(s)", "Duty%", "RPM");

    unsigned int elapsed = 0;
    while (1) {
        PWM_INFO info = { .channel_num = channel };
        if (ioctl(fd, PWM_CMD_GET_DUTY_RATIO, &info) < 0) {
            fprintf(stderr, "ioctl failed: %s\n", strerror(errno));
            break;
        }
        printf("%-12u %-8u %-10u\n", elapsed, info.ratio, info.speed);
        fflush(stdout);
        sleep(interval);
        elapsed += interval;
    }

    close(fd);
    return 0;
}

static void usage(const char *prog)
{
    printf("Usage: %s <command> [args]\n\n", prog);
    printf("Commands:\n");
    printf("  get [ch]          Get duty ratio and speed (default ch=0)\n");
    printf("  set <ratio> [ch]  Set duty ratio 0-100%% (default ch=0)\n");
    printf("  speed [ch]        Get fan speed in RPM\n");
    printf("  auto              Enable auto temperature adjustment\n");
    printf("  manual            Switch to manual mode\n");
    printf("  mode              Show current mode\n");
    printf("  monitor [sec]     Monitor fan status (default 2s interval)\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "get") == 0) {
        int ch = (argc > 2) ? atoi(argv[2]) : PWM_CHANNEL;
        return cmd_get(ch);
    }

    if (strcmp(cmd, "set") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s set <ratio> [channel]\n", argv[0]);
            return 1;
        }
        unsigned int ratio = atoi(argv[2]);
        int ch = (argc > 3) ? atoi(argv[3]) : PWM_CHANNEL;
        return cmd_set(ch, ratio);
    }

    if (strcmp(cmd, "speed") == 0) {
        int ch = (argc > 2) ? atoi(argv[2]) : PWM_CHANNEL;
        return cmd_speed(ch);
    }

    if (strcmp(cmd, "auto") == 0) {
        return cmd_mode_set(PWM_AUTO);
    }

    if (strcmp(cmd, "manual") == 0) {
        return cmd_mode_set(PWM_MANUAL);
    }

    if (strcmp(cmd, "mode") == 0) {
        return cmd_mode_get();
    }

    if (strcmp(cmd, "monitor") == 0) {
        int interval = (argc > 2) ? atoi(argv[2]) : 2;
        int ch = (argc > 3) ? atoi(argv[3]) : PWM_CHANNEL;
        if (interval < 1) interval = 1;
        return cmd_monitor(ch, interval);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    return 1;
}
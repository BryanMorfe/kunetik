#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdbool.h>

/* Device IO */
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "../ukunetik.h"

#define KTK_DEV "/dev/kunetik"

#define exit_msg(msg, status) { \
    printf("%s", msg); \
    exit(status); \
}

struct kunetik_data
{
    uint8_t temperature;
    float   humidity;
};

bool should_run = true;

static void get_data(int fd, uint8_t* temp_buf, size_t buf_len, struct kunetik_data* kdata)
{
    int ret;

    ret = ioctl(fd, KTK_CAPTURE_DATA);

    if (ret < 0)
    {
        printf("failed to ask device to capture data, reading last captured data...\n");
    }
    
    ret = read(fd, temp_buf, buf_len);

    if (ret > 0)
    {
        kdata->temperature = temp_buf[KTK_TEMP_OFFSET];
        kdata->humidity    = 100.0f * (float)temp_buf[KTK_HMDT_OFFSET] / UINT8_MAX;
    }
}

static void print_data(struct kunetik_data* kdata, struct kunetik_temp_type* temp_type)
{
    char temp_unit = temp_type->type == KTK_TEMP_TYPE_FAHRENHEIT ? 'F' : 'C';

    printf("\033[1A");
    printf("\033[2K");
    printf("\rHumidity | Temperature\n");
    printf("\033[2K");
    printf("\r%7.2f%% | %u %c", kdata->humidity, kdata->temperature, temp_unit);
}

void sighdlr(int signo)
{
    if (signo == SIGINT || signo == SIGQUIT || signo == SIGTSTP)
        should_run = false;
}

int main(int argc, char* argv[])
{
    int                      fd;
    struct kunetik_temp_type temp_type;
    struct kunetik_data      kdata;
    uint8_t                  buffer[KTK_DATA_SIZE];

    if (signal(SIGINT, sighdlr) == SIG_ERR || signal(SIGQUIT, sighdlr) == SIG_ERR
        || signal(SIGTSTP, sighdlr) == SIG_ERR)
    {
        exit_msg("failed to set signals\n", EXIT_FAILURE);
    }

    temp_type.type = KTK_TEMP_TYPE_CELCIUS;

    if (argc == 2)
    {
        if (strcasecmp(argv[1], "temp=f") == 0)
        {
            temp_type.type = KTK_TEMP_TYPE_FAHRENHEIT;
            printf("Temperature set to Fahrenheit.\n");
        }
        else if (strcasecmp(argv[1], "temp=c") == 0)
        {
            temp_type.type = KTK_TEMP_TYPE_CELCIUS;
            printf("Temperature set to Celcius.\n");
        }
        else
        {
            printf("unrecognized argument: %s\n", argv[1]);
            exit_msg("Invalid format, expected ./kunetikc [temp=(c|f)]\n", EXIT_FAILURE);
        }
    }
    else if (argc != 1)
    {
        exit_msg("Invalid format, expected ./kunetikc [temp=(c|f)]\n", EXIT_FAILURE);
    }

    fd = open(KTK_DEV, O_RDWR);

    if (fd < 0)
    {
        exit_msg("failed to open kunetik device\n", EXIT_FAILURE);
    }

    if (ioctl(fd, KTK_SET_TEMP_TYPE, &temp_type) < 0)
    {
        exit_msg("failed to set temperature type\n", EXIT_FAILURE);
    }

    while (should_run)
    {
        get_data(fd, buffer, KTK_DATA_SIZE, &kdata);
        print_data(&kdata, &temp_type);
        sleep(1);
    }

    printf("\nDone.\n");

    close(fd);

    return 0;
}
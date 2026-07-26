#include <stdio.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

int main(void)
{
    const char *license = avutil_license();
    const char *version = av_version_info();

    printf("FFmpeg version: %s\n", version);
    printf("FFmpeg license: %s\n", license);

    if (strncmp(version, "8.1.2", 5) != 0) {
        fprintf(stderr, "Unexpected FFmpeg version.\n");
        return 1;
    }
    if (strstr(license, "GPL") != NULL && strstr(license, "LGPL") == NULL) {
        fprintf(stderr, "GPL-only FFmpeg build is not accepted.\n");
        return 2;
    }
    if (avformat_network_init() < 0) {
        fprintf(stderr, "avformat_network_init failed.\n");
        return 3;
    }

    avformat_network_deinit();
    return 0;
}

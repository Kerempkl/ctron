#include "daeboard.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, name)                                                   \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s\n", name);                             \
            failures++;                                                     \
        }                                                                   \
    } while (0)

int main(void)
{
    char buf[32];
    char cmd[32], val[64];
    const char *text =
        "[enter]\n"
        "down = color 000000 120\n"
        "[f6]\n"
        "ctron = profile performance\n";

    CHECK(db_fmt_brightness(3, buf, (int)sizeof buf) == 0, "bri ok");
    CHECK(strcmp(buf, "brightness 3") == 0, "bri bytes");
    CHECK(db_fmt_brightness(9, buf, (int)sizeof buf) != 0, "bri range");
    CHECK(db_fmt_color("ccfffe", buf, (int)sizeof buf) == 0, "color ok");
    CHECK(strcmp(buf, "color ccfffe") == 0, "color bytes");
    CHECK(db_fmt_color("zzfffe", buf, (int)sizeof buf) != 0, "color hex");
    CHECK(db_action_in(text, "f6", cmd, (int)sizeof cmd, val, (int)sizeof val) == 0,
          "action found");
    CHECK(strcmp(cmd, "profile") == 0 && strcmp(val, "performance") == 0, "action pair");
    CHECK(db_action_in(text, "enter", cmd, (int)sizeof cmd, val, (int)sizeof val) != 0,
          "light row has no ctron action");
    if (failures) {
        fprintf(stderr, "%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}

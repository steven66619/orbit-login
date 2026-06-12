#include "test_runner.h"

#define CONF_TEST_PATH "/tmp/orbit-test-conf-XXXXXX"

static int test_defaults(void) {
    TEST("config_defaults sets expected defaults") {
        orbit_config_t cfg;
        memset(&cfg, 0xFF, sizeof(cfg));
        config_defaults(&cfg);
        ASSERT_EQ(cfg.verbose, 0, "verbose defaults to 0");
        ASSERT_EQ(cfg.auto_vt, 1, "auto_vt defaults to 1");
        ASSERT_EQ(cfg.vt_number, 7, "vt_number defaults to 7");
        ASSERT_STR_EQ(cfg.xorg_path, "/usr/bin/Xorg", "xorg_path default");
        ASSERT_STR_EQ(cfg.xauth_path, "/tmp/orbit-xauth", "xauth_path default");
        ASSERT_EQ(cfg.min_uid, 1000, "min_uid default");
        ASSERT_EQ(cfg.max_uid, 65000, "max_uid default");
        ASSERT_STR_EQ(cfg.greeter_user, "root", "greeter_user default");
        ASSERT_EQ(cfg.session_timeout, 30, "session_timeout default");
    }
    END_TEST;
    return tests_failed;
}

static int test_parse_empty(void) {
    TEST("config_load with missing file uses defaults") {
        orbit_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        int ret = config_load("/tmp/nonexistent-orbit-conf", &cfg);
        ASSERT_EQ(ret, 0, "returns 0 for missing file");
        ASSERT_EQ(cfg.verbose, 0, "verbose is default");
    }
    END_TEST;
    return tests_failed;
}

static int test_parse_basic_conf(void) {
    TEST("config_load parses basic config") {
        char tmpl[] = CONF_TEST_PATH;
        int fd = mkstemp(tmpl);
        ASSERT(fd >= 0, "temp file created");
        const char *content =
            "Verbose = 1\n"
            "VTNumber = 3\n"
            "MinUid = 500\n"
            "MaxUid = 60000\n"
            "SessionTimeout = 60\n";
        ASSERT(write(fd, content, strlen(content)) == (ssize_t)strlen(content),
               "write content");
        close(fd);

        orbit_config_t cfg;
        config_load(tmpl, &cfg);
        ASSERT_EQ(cfg.verbose, 1, "verbose parsed");
        ASSERT_EQ(cfg.vt_number, 3, "vt_number parsed");
        ASSERT_EQ(cfg.auto_vt, 0, "auto_vt disabled when VTNumber set");
        ASSERT_EQ(cfg.min_uid, 500, "min_uid parsed");
        ASSERT_EQ(cfg.max_uid, 60000, "max_uid parsed");
        ASSERT_EQ(cfg.session_timeout, 60, "session_timeout parsed");
        ASSERT_STR_EQ(cfg.xorg_path, "/usr/bin/Xorg", "xorg_path unchanged");

        unlink(tmpl);
    }
    END_TEST;
    return tests_failed;
}

static int test_parse_comments_and_sections(void) {
    TEST("config_load handles comments and sections") {
        char tmpl[] = CONF_TEST_PATH;
        int fd = mkstemp(tmpl);
        ASSERT(fd >= 0, "temp file created");
        const char *content =
            "# This is a comment\n"
            "[section]\n"
            "Verbose = 1\n"
            "  # indented comment\n"
            "GreeterUser = greeter\n";
        ASSERT(write(fd, content, strlen(content)) == (ssize_t)strlen(content),
               "write content");
        close(fd);

        orbit_config_t cfg;
        config_load(tmpl, &cfg);
        ASSERT_EQ(cfg.verbose, 1, "verbose parsed after section");
        ASSERT_STR_EQ(cfg.greeter_user, "greeter", "greeter_user parsed");

        unlink(tmpl);
    }
    END_TEST;
    return tests_failed;
}

static int test_parse_invalid_values(void) {
    TEST("config_load handles invalid values gracefully") {
        char tmpl[] = CONF_TEST_PATH;
        int fd = mkstemp(tmpl);
        ASSERT(fd >= 0, "temp file created");
        const char *content =
            "Verbose = notanumber\n"
            "VTNumber = -1\n"
            "MinUid = abc\n";
        ASSERT(write(fd, content, strlen(content)) == (ssize_t)strlen(content),
               "write content");
        close(fd);

        orbit_config_t cfg;
        config_load(tmpl, &cfg);
        ASSERT_EQ(cfg.verbose, 0, "verbose defaults on invalid");
        ASSERT_EQ(cfg.auto_vt, 1, "auto_vt kept when VTNumber is invalid");
        ASSERT_EQ(cfg.vt_number, 7, "vt_number defaults on invalid");
        ASSERT_EQ(cfg.min_uid, 1000, "min_uid defaults on invalid");

        unlink(tmpl);
    }
    END_TEST;
    return tests_failed;
}

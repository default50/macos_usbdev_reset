// Unit tests for devreset_run(), driven by the fake usb_ops.
//
// Uses greatest v1.5.0 (tests/greatest.h, ISC), vendored unmodified from
// https://raw.githubusercontent.com/silentbicycle/greatest/v1.5.0/greatest.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "greatest.h"
#include "fake_usb_ops.h"
#include "usb_ops.h"

#define VID 0x1050
#define PID 0x0407

#define LOOKING "Looking for productID=0x0407 vendorID=0x1050\n"
#define FOUND0 "Found device vendor=0x1050, product=0x0407, version=0x0543, locationID=0x01100000\n"
#define FOUND1 "Found device vendor=0x1050, product=0x0407, version=0x0574, locationID=0x00100000\n"

static char g_out[8192], g_err[8192];

static void slurp (FILE *f, char *buf, size_t size) {
  rewind(f);
  size_t n = fread(buf, 1, size - 1, f);
  buf[n] = '\0';
  fclose(f);
}

// Run devreset_run with stdout/stderr captured into g_out/g_err.
static int run_args (const char **args) {
  char *argv[16];
  int argc = 0;
  while (args[argc]) {
    argv[argc] = (char *)args[argc];
    argc++;
  }
  argv[argc] = NULL;

  FILE *out = tmpfile(), *err = tmpfile();
  if (!out || !err) abort();
  fflush(stdout);
  fflush(stderr);
  int saved_out = dup(1), saved_err = dup(2);
  dup2(fileno(out), 1);
  dup2(fileno(err), 2);

  int rc = devreset_run(argc, argv, &fake_usb_ops);

  fflush(stdout);
  fflush(stderr);
  dup2(saved_out, 1);
  dup2(saved_err, 2);
  close(saved_out);
  close(saved_err);
  slurp(out, g_out, sizeof g_out);
  slurp(err, g_err, sizeof g_err);
  return rc;
}

#define RUN(...) run_args((const char *[]){ "devreset", __VA_ARGS__, NULL })
#define RUN0() run_args((const char *[]){ "devreset", NULL })

static void setup_cb (void *data) {
  (void)data;
  fake_reset();
  g_out[0] = '\0';
  g_err[0] = '\0';
}

// Two devices sharing VID/PID, as in the README example.
static void add_two (void) {
  fake_add_device(VID, PID, 0x0543, 0x01100000);
  fake_add_device(VID, PID, 0x0574, 0x00100000);
}

/* ---- usage: argument errors exit 1 before touching devices ---- */

#define ASSERT_USAGE_ERROR(rc) do { \
    ASSERT_EQ_FMT(1, (rc), "%d"); \
    ASSERT_EQ_FMT(0, fake_find_calls(), "%d"); \
    ASSERT_STR_EQ("", g_out); \
    ASSERT_EQ_FMT(0, fake_outstanding(), "%d"); \
  } while (0)

TEST no_args (void) {
  ASSERT_USAGE_ERROR(RUN0());
  ASSERT_EQ(0, strncmp(g_err, "usage: devreset", 15));
  PASS();
}

TEST one_positional (void) {
  ASSERT_USAGE_ERROR(RUN("0x0407"));
  ASSERT_EQ(0, strncmp(g_err, "usage: devreset", 15));
  PASS();
}

TEST unknown_option (void) {
  ASSERT_USAGE_ERROR(RUN("-x", "0x0407", "0x1050"));
  ASSERT(strstr(g_err, "usage: devreset"));
  PASS();
}

TEST location_missing_value (void) {
  ASSERT_USAGE_ERROR(RUN("-l"));
  ASSERT(strstr(g_err, "usage: devreset"));
  PASS();
}

TEST location_bad_hex (void) {
  ASSERT_USAGE_ERROR(RUN("-l", "zz", "0x0407", "0x1050"));
  ASSERT_STR_EQ("Invalid locationID\n", g_err);
  PASS();
}

TEST location_too_big (void) {
  ASSERT_USAGE_ERROR(RUN("-l", "0x100000000", "0x0407", "0x1050"));
  ASSERT_STR_EQ("Invalid locationID\n", g_err);
  PASS();
}

TEST release_too_big (void) {
  ASSERT_USAGE_ERROR(RUN("-r", "0x10000", "0x0407", "0x1050"));
  ASSERT_STR_EQ("Invalid release\n", g_err);
  PASS();
}

TEST product_bad_hex (void) {
  ASSERT_USAGE_ERROR(RUN("zz", "0x1050"));
  ASSERT_STR_EQ("Invalid productID or vendorID\n", g_err);
  PASS();
}

TEST vendor_too_big (void) {
  ASSERT_USAGE_ERROR(RUN("0x0407", "0x10000"));
  ASSERT_STR_EQ("Invalid productID or vendorID\n", g_err);
  PASS();
}

TEST product_zero (void) {
  ASSERT_USAGE_ERROR(RUN("0", "0x1050"));
  ASSERT_STR_EQ("Invalid productID or vendorID\n", g_err);
  PASS();
}

// BSD getopt stops at the first non-option, so a trailing -n is ignored
// and the device is reset. Options must go before the IDs.
TEST option_after_ids_is_ignored (void) {
  fake_add_device(VID, PID, 0x0543, 0x01100000);
  ASSERT_EQ_FMT(0, RUN("0x0407", "0x1050", "-n"), "%d");
  ASSERT_EQ_FMT(1, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

SUITE(usage) {
  SET_SETUP(setup_cb, NULL);
  RUN_TEST(no_args);
  RUN_TEST(one_positional);
  RUN_TEST(unknown_option);
  RUN_TEST(location_missing_value);
  RUN_TEST(location_bad_hex);
  RUN_TEST(location_too_big);
  RUN_TEST(release_too_big);
  RUN_TEST(product_bad_hex);
  RUN_TEST(vendor_too_big);
  RUN_TEST(product_zero);
  RUN_TEST(option_after_ids_is_ignored);
}

/* ---- matching ---- */

TEST no_devices (void) {
  ASSERT_EQ_FMT(2, RUN("0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING, g_out);
  ASSERT_STR_EQ("No matching device found\n", g_err);
  ASSERT_EQ_FMT(1, fake_find_calls(), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST only_decoys (void) {
  int a = fake_add_device(VID, 0x0001, 0x0543, 0x01100000);
  int b = fake_add_device(0x0001, PID, 0x0543, 0x00100000);
  ASSERT_EQ_FMT(2, RUN("0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING, g_out);
  ASSERT_STR_EQ("No matching device found\n", g_err);
  ASSERT_EQ_FMT(0, fake_describe_calls(a), "%d");
  ASSERT_EQ_FMT(0, fake_describe_calls(b), "%d");
  ASSERT_EQ_FMT(0, fake_reset_calls(a), "%d");
  ASSERT_EQ_FMT(0, fake_reset_calls(b), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST find_fails (void) {
  fake_add_device(VID, PID, 0x0543, 0x01100000);
  fake_fail_find();
  ASSERT_EQ_FMT(1, RUN("0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING, g_out);
  ASSERT_STR_EQ("", g_err);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST decimal_ids (void) {
  fake_add_device(VID, PID, 0x0543, 0x01100000);
  ASSERT_EQ_FMT(0, RUN("1031", "4176"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND0, g_out);
  ASSERT_EQ_FMT(1, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

SUITE(matching) {
  SET_SETUP(setup_cb, NULL);
  RUN_TEST(no_devices);
  RUN_TEST(only_decoys);
  RUN_TEST(find_fails);
  RUN_TEST(decimal_ids);
}

/* ---- reset ---- */

TEST single_device (void) {
  fake_add_device(VID, PID, 0x0543, 0x01100000);
  ASSERT_EQ_FMT(0, RUN("0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND0, g_out);
  ASSERT_STR_EQ("", g_err);
  ASSERT_EQ_FMT(1, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

// Regression: devices sharing a VID/PID used to be reset repeatedly.
TEST shared_vid_pid_each_reset_once (void) {
  for (int i = 0; i < 3; i++)
    fake_add_device(VID, PID, 0x0543, 0x01100000 + (uint32_t)i);
  ASSERT_EQ_FMT(0, RUN("0x0407", "0x1050"), "%d");
  for (int i = 0; i < 3; i++)
    ASSERT_EQ_FMT(1, fake_reset_calls(i), "%d");
  ASSERT_EQ_FMT(1, fake_find_calls(), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

// A failed reset doesn't stop the loop; the others are still reset.
TEST reset_failure_continues (void) {
  for (int i = 0; i < 3; i++)
    fake_add_device(VID, PID, 0x0543, 0x01100000 + (uint32_t)i);
  fake_fail_reset(1);
  ASSERT_EQ_FMT(3, RUN("0x0407", "0x1050"), "%d");
  for (int i = 0; i < 3; i++)
    ASSERT_EQ_FMT(1, fake_reset_calls(i), "%d");
  ASSERT_FALSE(strstr(g_err, "No matching"));
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST all_resets_fail (void) {
  add_two();
  fake_fail_reset(0);
  fake_fail_reset(1);
  ASSERT_EQ_FMT(3, RUN("0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND0 FOUND1, g_out);
  ASSERT_STR_EQ("", g_err);
  ASSERT_EQ_FMT(1, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(1, fake_reset_calls(1), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST describe_failure (void) {
  add_two();
  fake_fail_describe(0);
  ASSERT_EQ_FMT(3, RUN("0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND1, g_out);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(1, fake_reset_calls(1), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST decoy_never_reset (void) {
  fake_add_device(VID, PID, 0x0543, 0x01100000);
  int decoy = fake_add_device(VID, 0x0408, 0x0543, 0x00100000);
  ASSERT_EQ_FMT(0, RUN("0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND0, g_out);
  ASSERT_EQ_FMT(1, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_reset_calls(decoy), "%d");
  ASSERT_EQ_FMT(0, fake_describe_calls(decoy), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

SUITE(reset) {
  SET_SETUP(setup_cb, NULL);
  RUN_TEST(single_device);
  RUN_TEST(shared_vid_pid_each_reset_once);
  RUN_TEST(reset_failure_continues);
  RUN_TEST(all_resets_fail);
  RUN_TEST(describe_failure);
  RUN_TEST(decoy_never_reset);
}

/* ---- selection: -n, -l, -r ---- */

TEST list_only_resets_nothing (void) {
  add_two();
  ASSERT_EQ_FMT(0, RUN("-n", "0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND0 FOUND1, g_out);
  ASSERT_STR_EQ("", g_err);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_reset_calls(1), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST list_only_no_match (void) {
  ASSERT_EQ_FMT(2, RUN("-n", "0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING, g_out);
  ASSERT_STR_EQ("No matching device found\n", g_err);
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST list_only_describe_failure (void) {
  add_two();
  fake_fail_describe(1);
  ASSERT_EQ_FMT(3, RUN("-n", "0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND0, g_out);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST location_selects_one (void) {
  add_two();
  ASSERT_EQ_FMT(0, RUN("-l", "0x00100000", "0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND1, g_out);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(1, fake_reset_calls(1), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST location_unknown (void) {
  add_two();
  ASSERT_EQ_FMT(2, RUN("-l", "0x02200000", "0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING, g_out);
  ASSERT_STR_EQ("No matching device found\n", g_err);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_reset_calls(1), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST release_selects_one (void) {
  add_two();
  ASSERT_EQ_FMT(0, RUN("-r", "0x0574", "0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND1, g_out);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(1, fake_reset_calls(1), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST location_and_release_are_anded (void) {
  add_two();
  ASSERT_EQ_FMT(2, RUN("-l", "0x01100000", "-r", "0x0574", "0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING, g_out);
  ASSERT_STR_EQ("No matching device found\n", g_err);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_reset_calls(1), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

TEST list_with_location (void) {
  add_two();
  ASSERT_EQ_FMT(0, RUN("-n", "-l", "0x01100000", "0x0407", "0x1050"), "%d");
  ASSERT_STR_EQ(LOOKING FOUND0, g_out);
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_reset_calls(1), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

SUITE(selection) {
  SET_SETUP(setup_cb, NULL);
  RUN_TEST(list_only_resets_nothing);
  RUN_TEST(list_only_no_match);
  RUN_TEST(list_only_describe_failure);
  RUN_TEST(location_selects_one);
  RUN_TEST(location_unknown);
  RUN_TEST(release_selects_one);
  RUN_TEST(location_and_release_are_anded);
  RUN_TEST(list_with_location);
}

/* ---- repeat: no state leaks between devreset_run calls ---- */

TEST list_then_reset (void) {
  fake_add_device(VID, PID, 0x0543, 0x01100000);
  ASSERT_EQ_FMT(0, RUN("-n", "0x0407", "0x1050"), "%d");
  ASSERT_EQ_FMT(0, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, RUN("0x0407", "0x1050"), "%d");
  ASSERT_EQ_FMT(1, fake_reset_calls(0), "%d");
  ASSERT_EQ_FMT(0, fake_outstanding(), "%d");
  PASS();
}

SUITE(repeat) {
  SET_SETUP(setup_cb, NULL);
  RUN_TEST(list_then_reset);
}

GREATEST_MAIN_DEFS();

int main (int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(usage);
  RUN_SUITE(matching);
  RUN_SUITE(reset);
  RUN_SUITE(selection);
  RUN_SUITE(repeat);
  GREATEST_MAIN_END();
}

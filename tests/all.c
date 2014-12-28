#include "tests.h"

#include "audio/chmap.h"
#include "audio/chmap_sel.h"

static void test_mp_chmap_diff(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;
    struct mp_chmap diff;

    mp_chmap_from_str(&a, bstr0("3.1"));
    mp_chmap_from_str(&b, bstr0("2.1"));

    mp_chmap_diff(&a, &b, &diff);
    assert_int_equal(diff.num, 1);
    assert_int_equal(diff.speaker[0], MP_SPEAKER_ID_FC);

    mp_chmap_from_str(&b, bstr0("6.1(back)"));
    mp_chmap_diff(&a, &b, &diff);
    assert_int_equal(diff.num, 0);

    mp_chmap_diff(&b, &a, &diff);
    assert_int_equal(diff.num, 3);
    assert_int_equal(diff.speaker[0], MP_SPEAKER_ID_BL);
    assert_int_equal(diff.speaker[1], MP_SPEAKER_ID_BR);
    assert_int_equal(diff.speaker[2], MP_SPEAKER_ID_BC);
}

static void test_mp_chmap_contains_with_related_chmaps(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;

    mp_chmap_from_str(&a, bstr0("3.1"));
    mp_chmap_from_str(&b, bstr0("2.1"));

    assert_true(mp_chmap_contains(&a, &b));
    assert_false(mp_chmap_contains(&b, &a));
}

static void test_mp_chmap_contains_with_unrelated_chmaps(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;

    mp_chmap_from_str(&a, bstr0("mono"));
    mp_chmap_from_str(&b, bstr0("stereo"));

    assert_false(mp_chmap_contains(&a, &b));
    assert_false(mp_chmap_contains(&b, &a));
}

static void test_mp_chmap_sel_upmix(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;
    struct mp_chmap_sel s = {0};

    mp_chmap_from_str(&a, bstr0("7.1"));
    mp_chmap_from_str(&b, bstr0("5.1"));

    mp_chmap_sel_add_map(&s, &a);
    assert_true(mp_chmap_sel_fallback(&s, &b));
    assert_string_equal(mp_chmap_to_str(&b), "7.1");
}

static void test_mp_chmap_sel_downmix(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;
    struct mp_chmap_sel s = {0};

    mp_chmap_from_str(&a, bstr0("5.1"));
    mp_chmap_from_str(&b, bstr0("7.1"));

    mp_chmap_sel_add_map(&s, &a);
    assert_true(mp_chmap_sel_fallback(&s, &b));
    assert_string_equal(mp_chmap_to_str(&b), "5.1");
}

static void test_mp_chmap_sel_incompatible(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;
    struct mp_chmap_sel s = {0};

    mp_chmap_from_str(&a, bstr0("stereo"));
    mp_chmap_from_str(&b, bstr0("mono"));

    mp_chmap_sel_add_map(&s, &a);
    assert_false(mp_chmap_sel_fallback(&s, &b));
}

static void test_mp_chmap_sel_prefer_closest_upmix(void **state) {
    struct mp_chmap_sel s = {0};

    char *maps[] = { "7.1", "5.1", "2.1", "stereo", "mono", NULL };
    for (int i = 0; maps[i]; i++) {
        struct mp_chmap m;
        mp_chmap_from_str(&m, bstr0(maps[i]));
        mp_chmap_sel_add_map(&s, &m);
    }

    struct mp_chmap c;
    mp_chmap_from_str(&c, bstr0("3.1"));
    assert_true(mp_chmap_sel_fallback(&s, &c));
    assert_string_equal(mp_chmap_to_str(&c), "5.1");
}

static void test_mp_chmap_sel_use_replacements(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;
    struct mp_chmap_sel s = {0};

    mp_chmap_from_str(&a, bstr0("7.1(rear)"));
    mp_chmap_from_str(&b, bstr0("5.1"));

    mp_chmap_sel_add_map(&s, &a);
    assert_true(mp_chmap_sel_fallback(&s, &b));
    assert_string_equal(mp_chmap_to_str(&b), "7.1(rear)");
}

static void test_mp_chmap_sel_fallback_reject_unknown(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;
    struct mp_chmap_sel s = {0};

    a.num = 2;
    a.speaker[0] = MP_SPEAKER_ID_UNKNOWN0;
    a.speaker[1] = MP_SPEAKER_ID_UNKNOWN0 + 1;

    mp_chmap_from_str(&b, bstr0("5.1"));

    mp_chmap_sel_add_map(&s, &a);
    assert_false(mp_chmap_sel_fallback(&s, &b));
    assert_string_equal(mp_chmap_to_str(&b), "5.1");
}

static void test_mp_chmap_sel_fallback_reject_non_lavc_chmaps(void **state) {
    struct mp_chmap a;
    struct mp_chmap b;
    struct mp_chmap_sel s = {0};

    mp_chmap_from_str(&a, bstr0("7.1"));
    int tmp = a.speaker[0];
    a.speaker[0] = a.speaker[1];
    a.speaker[1] = tmp;

    mp_chmap_from_str(&b, bstr0("5.1"));

    mp_chmap_sel_add_map(&s, &a);
    assert_false(mp_chmap_sel_fallback(&s, &b));
    assert_string_equal(mp_chmap_to_str(&b), "5.1");
}

int main(void) {
    const UnitTest tests[] = {
        unit_test(test_mp_chmap_diff),
        unit_test(test_mp_chmap_contains_with_related_chmaps),
        unit_test(test_mp_chmap_contains_with_unrelated_chmaps),

        unit_test(test_mp_chmap_sel_upmix),
        unit_test(test_mp_chmap_sel_downmix),
        unit_test(test_mp_chmap_sel_incompatible),
        unit_test(test_mp_chmap_sel_prefer_closest_upmix),
        unit_test(test_mp_chmap_sel_use_replacements),

        unit_test(test_mp_chmap_sel_fallback_reject_unknown),
        unit_test(test_mp_chmap_sel_fallback_reject_non_lavc_chmaps),
    };
    return run_tests(tests);
}

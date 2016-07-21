// -*- mode: c++; c-basic-offset: 4 indent-tabs-mode: nil -*- */
//
// Copyright 2016 Juho Snellman, released under a MIT license (see
// LICENSE).

#include <functional>

#include "../timer-wheel.h"

#define TEST(fun) \
    do {                                              \
        if (fun()) {                                  \
            printf("[OK] %s\n", #fun);                \
        } else {                                      \
            ok = false;                               \
            printf("[FAILED] %s\n", #fun);            \
        }                                             \
    } while (0)

#define EXPECT(expr)                                    \
    do {                                                \
        if (!(expr))  {                                 \
            printf("%s:%d: Expect failed: %s\n",        \
                   __FILE__, __LINE__, #expr);          \
            return false;                               \
        }                                               \
    } while (0)

#define EXPECT_INTEQ(actual, expect)                    \
    do {                                                \
        if (expect != actual)  {                        \
            printf("%s:%d: Expect failed, wanted %ld"   \
                   " got %ld\n",                        \
                   __FILE__, __LINE__,                  \
                   (long) expect, (long) actual);       \
            return false;                               \
        }                                               \
    } while (0)

bool test_single_timer_no_hierarchy() {
    typedef std::function<void()> Callback;
    TimerWheel timers;
    int count = 0;
    TimerEvent<Callback> timer([&count] () { ++count; });

    // Unscheduled timer does nothing.
    timers.advance(10);
    EXPECT_INTEQ(count, 0);
    EXPECT(!timer.active());

    // Schedule timer, should trigger at right time.
    timers.schedule(&timer, 5);
    EXPECT(timer.active());
    timers.advance(5);
    EXPECT_INTEQ(count, 1);

    // Only trigger once, not repeatedly (even if wheel wraps
    // around).
    timers.advance(256);
    EXPECT_INTEQ(count, 1);

    // ... unless, of course, the timer gets scheduled again.
    timers.schedule(&timer, 5);
    timers.advance(5);
    EXPECT_INTEQ(count, 2);

    // Canceled timers don't run.
    timers.schedule(&timer, 5);
    timer.cancel();
    EXPECT(!timer.active());
    timers.advance(10);
    EXPECT_INTEQ(count, 2);

    // Test wraparound
    timers.advance(250);
    timers.schedule(&timer, 5);
    timers.advance(10);
    EXPECT_INTEQ(count, 3);

    // Timers that are scheduled multiple times only run at the last
    // scheduled tick.
    timers.schedule(&timer, 5);
    timers.schedule(&timer, 10);
    timers.advance(5);
    EXPECT_INTEQ(count, 3);
    timers.advance(5);
    EXPECT_INTEQ(count, 4);

    // Timer can safely be canceled multiple times.
    timers.schedule(&timer, 5);
    timer.cancel();
    timer.cancel();
    EXPECT(!timer.active());
    timers.advance(10);
    EXPECT_INTEQ(count, 4);

    return true;
}

bool test_single_timer_hierarchy() {
    typedef std::function<void()> Callback;
    TimerWheel timers;
    int count = 0;
    TimerEvent<Callback> timer([&count] () { ++count; });

    EXPECT_INTEQ(count, 0);

    // Schedule timer one layer up (make sure timer ends up in slot 0 once
    // promoted to the innermost wheel, since that's a special case).
    timers.schedule(&timer, 256);
    timers.advance(255);
    EXPECT_INTEQ(count, 0);
    timers.advance(1);
    EXPECT_INTEQ(count, 1);

    // Then schedule one that ends up in some other slot
    timers.schedule(&timer, 257);
    timers.advance(256);
    EXPECT_INTEQ(count, 1);
    timers.advance(1);
    EXPECT_INTEQ(count, 2);

    // Schedule multiple rotations ahead in time, to slot 0.
    timers.schedule(&timer, 256*4 - 1);
    timers.advance(256*4 - 2);
    EXPECT_INTEQ(count, 2);
    timers.advance(1);
    EXPECT_INTEQ(count, 3);

    // Schedule multiple rotations ahead in time, to non-0 slot. (Do this
    // twice, once starting from slot 0, once starting from slot 5);
    for (int i = 0; i < 2; ++i) {
        timers.schedule(&timer, 256*4 + 5);
        timers.advance(256*4 + 4);
        EXPECT_INTEQ(count, 3 + i);
        timers.advance(1);
        EXPECT_INTEQ(count, 4 + i);
    }

    return true;
}

bool test_ticks_to_next_event() {
    typedef std::function<void()> Callback;
    TimerWheel timers;
    TimerEvent<Callback> timer([] () { });
    TimerEvent<Callback> timer2([] () { });

    // No timers scheduled, return the max value.
    EXPECT_INTEQ(timers.ticks_to_next_event(100), 100);
    EXPECT_INTEQ(timers.ticks_to_next_event(0), 0);

    for (int i = 0; i < 10; ++i) {
        // Just vanilla tests
        timers.schedule(&timer, 1);
        EXPECT_INTEQ(timers.ticks_to_next_event(100), 1);

        timers.schedule(&timer, 20);
        EXPECT_INTEQ(timers.ticks_to_next_event(100), 20);

        // Check the the "max" parameters works.
        timers.schedule(&timer, 150);
        EXPECT_INTEQ(timers.ticks_to_next_event(100), 100);

        // Check that a timer on the next layer can be found.
        timers.schedule(&timer, 280);
        EXPECT_INTEQ(timers.ticks_to_next_event(100), 100);
        EXPECT_INTEQ(timers.ticks_to_next_event(1000), 280);

        // Test having a timer on the next wheel (still remaining from
        // the previous test), and another (earlier) timer on this
        // wheel.
        for (int i = 1; i < 256; ++i) {
            timers.schedule(&timer2, i);
            EXPECT_INTEQ(timers.ticks_to_next_event(1000), i);
        }

        timer.cancel();
        timer2.cancel();
        // And then run these same tests from a bunch of different
        // wheel locations.
        timers.advance(32);
    }

    // More thorough tests for cases where the next timer could be on
    // either of two different wheels.
    for (int i = 0; i < 20; ++i) {
        timers.schedule(&timer, 270);
        timers.advance(128);
        EXPECT_INTEQ(timers.ticks_to_next_event(512), 270 - 128);
        timers.schedule(&timer2, 250);
        EXPECT_INTEQ(timers.ticks_to_next_event(512), 270 - 128);
        timers.schedule(&timer2, 10);
        EXPECT_INTEQ(timers.ticks_to_next_event(512), 10);

        // Again, do this from a bunch of different locatoins.
        timers.advance(32);
    }

    return true;
}

bool test_reschedule_from_timer() {
    typedef std::function<void()> Callback;
    TimerWheel timers;
    int count = 0;
    TimerEvent<Callback> timer([&count] () { ++count; });

    // For every slot in the outermost wheel, try scheduling a timer from
    // a timer handler 258 ticks in the future. Then reschedule it in 257
    // ticks. It should never actually trigger.
    for (int i = 0; i < 256; ++i) {
        TimerEvent<Callback> rescheduler([&timers, &timer] () { timers.schedule(&timer, 258); });

        timers.schedule(&rescheduler, 1);
        timers.advance(257);
        EXPECT_INTEQ(count, 0);
    }
    // But once we stop rescheduling the timer, it'll trigger as intended.
    timers.advance(2);
    EXPECT_INTEQ(count, 1);

    return true;
}

bool test_single_timer_random() {
    typedef std::function<void()> Callback;
    TimerWheel timers;
    int count = 0;
    TimerEvent<Callback> timer([&count] () { ++count; });

    for (int i = 0; i < 10000; ++i) {
        int len = rand() % 20;
        int r = 1 + rand() % ( 1 << len);

        timers.schedule(&timer, r);
        timers.advance(r - 1);
        EXPECT_INTEQ(count, i);
        timers.advance(1);
        EXPECT_INTEQ(count, i + 1);
    }

    return true;
}

class Test {
public:
    Test()
        : inc_timer_(this), reset_timer_(this) {
    }

    void start(TimerWheel* timers) {
        timers->schedule(&inc_timer_, 10);
        timers->schedule(&reset_timer_, 15);
    }

    void on_inc() {
        count_++;
    }

    void on_reset() {
        count_ = 0;
    }

    int count() { return count_; }

private:
    MemberTimerEvent<Test, &Test::on_inc> inc_timer_;
    MemberTimerEvent<Test, &Test::on_reset> reset_timer_;
    int count_ = 0;
};

bool test_timeout_method() {
    TimerWheel timers;

    Test test;
    test.start(&timers);

    EXPECT_INTEQ(test.count(), 0);
    timers.advance(10);
    EXPECT_INTEQ(test.count(), 1);
    timers.advance(5);
    EXPECT_INTEQ(test.count(), 0);
    return true;
}

int main(void) {
    bool ok = true;
    TEST(test_single_timer_no_hierarchy);
    TEST(test_single_timer_hierarchy);
    TEST(test_ticks_to_next_event);
    TEST(test_single_timer_random);
    TEST(test_reschedule_from_timer);
    TEST(test_timeout_method);
    // Test canceling timer from within timer
    return ok ? 0 : 1;
}

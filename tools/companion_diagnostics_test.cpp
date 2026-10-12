#include "../local/host/companion_diagnostics.h"
#include <cassert>
#include <thread>
#include <cstdio>

int main() {
    companion::TrialObservation trial{200, 300, 400};
    assert(!trial.mayCancel(0) && !trial.mayCancel(500));
    assert(trial.matches(200, 300, 400));
    assert(!trial.matches(201, 300, 400));
    assert(!trial.matches(200, 301, 400));
    assert(!trial.matches(200, 300, 401));
    assert(!trial.observe(0, false) && !trial.disappearance);
    assert(trial.observe(500, false) && !trial.lateActivation);
    assert(trial.mayCancel(500) && !trial.mayCancel(501));
    assert(trial.observe(501, false) && trial.replacement);
    assert(!trial.mayCancel(501) && !trial.mayCancel(500));
    assert(trial.observe(0, true) && trial.disappearance);
    assert(trial.observe(600, true) && trial.lateActivation);
    assert(!trial.mayCancel(600));
    companion::TrialObservation gone;
    gone.observe(500, false); gone.observe(0, false); gone.observe(600, false);
    assert(gone.disappearance && !gone.mayCancel(600));
    gone.observe(500, false);
    assert(!gone.mayCancel(500)); // same address returning after zero is still fenced
    assert(!companion::TrialObservation::finished(6000, 1000));
    assert(!companion::TrialObservation::finished(120999, 1000));
    assert(companion::TrialObservation::finished(121000, 1000));
    assert(!companion::TrialObservation::finished(999, 1000));
    companion::Diagnostics events;
    events.reserve(0); events.reserve(1);
    unsigned logged = 0;
    for (unsigned i = 0; i < 100; ++i)
        logged += events.observe("SCO_COMPANION_PROBE_EXECUTED", true, 2);
    assert(logged == 64);
    assert(!events.observe("SCO_COMPANION_SUCCESS_SCO_CompanionOrder_1", true, 2));
    assert(events.result(0) == companion::SuccessSeen);
    events.observe("SCO_COMPANION_FAILURE_SCO_CompanionOrder_1", true, 2);
    assert(events.result(0) == (companion::SuccessSeen | companion::FailureSeen));
    events.observe("SCO_COMPANION_FAILURE_SCO_CompanionOrder_2", true, 2);
    events.observe("SCO_COMPANION_SUCCESS_SCO_CompanionOrder_2", true, 2);
    assert(events.result(1) == events.result(0));
    events.loadFailed(1);
    assert(events.result(1) == 7); // load failure never aliases the two-callback history (3)
    events.reserve(2);
    events.observe("SCO_COMPANION_SUCCESS_SCO_CompanionOrder_3", false, 3);
    events.observe("SCO_COMPANION_SUCCESS_SCO_CompanionOrder_4", true, 3);
    assert(events.result(2) == 0 && events.result(3) == 0);

    companion::RequestQueue queue;
    queue.publish({9, 100, 200});
    const auto replaced = queue.publish({7, 300, 400});
    assert(replaced.action == 9 && replaced.npc == 100 && replaced.player == 200);
    const auto current = queue.consume();
    assert(current.matches(300, 400));
    assert(!current.matches(301, 400) && !current.matches(300, 401));
    assert(queue.consume().action == 0);

    // Exercise simultaneous publication/consumption: no action may be paired
    // with identities from a different request, even when pending requests merge.
    std::atomic<bool> done{false}; std::atomic<unsigned> observed{0};
    std::thread producer([&] {
        for (uint64_t i = 1; i <= 50000; ++i) queue.publish({9, i, i + 100000});
        done.store(true);
    });
    do {
        const auto request = queue.consume();
        if (request.action) { assert(request.action == 9 && request.player == request.npc + 100000); ++observed; }
    } while (!done.load());
    producer.join();
    const auto last = queue.consume();
    if (last.action) assert(last.player == last.npc + 100000);
    std::puts("PASS callback history, coherent request queue, target identity, bounded observation and refusal after assignment replacement/disappearance");
}

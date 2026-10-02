#include "../src/video_ack_metrics.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static void accounting(const video_stream::AckMetrics& m) {
    check(m.tracked == m.matched + m.unobserved + m.overwritten + m.abandoned + m.pending(), "exact submission accounting");
}
int main() {
    using namespace video_stream;
    AckMetrics m;
    check(!m.observe(0, 0), "INIT/control without submission is not a video ACK");
    m.sent(20, 100, 1000, false, 0);
    check(!m.observe(19, 1100) && !m.observe(21, 1100), "unknown echo cannot confirm frame");
    check(m.observe(20, 1600) && m.matched == 1 && m.last_sequence == 100 &&
        m.observation_delay.average_us() == 600 && m.progressive == 1, "exact match and observation delay");
    check(!m.observe(20, 1700) && m.matched == 1, "cached duplicate status counted once");
    m.sent(21, 101, 1800, true, 0);
    m.sent(22, 101, 1900, true, 1);
    check(m.observe(22, 2200) && m.fields == 1 && m.last_field == 1 && m.unobserved == 1,
        "latest status confirms only exact field, earlier is unobserved not lost");
    check(!m.observe(21, 2300), "reordered echo cannot count retired field");
    accounting(m);
    m.sent(0xffffffffu, 102, 2400, true, 0);
    m.sent(0, 102, 2500, true, 1);
    check(m.observe(0xffffffffu, 2600) && m.observe(0, 2700), "32-bit frame wrap matches exactly");
    m.sent(1, 103, 2800, false, 0);
    m.newSession();
    check(!m.observe(1, 3000) && m.abandoned == 1, "session reset clears outstanding correlation");
    m.sent(1, 104, 3100, false, 0);
    check(m.observe(1, 3200) && m.last_sequence == 104, "new session counters reusable");
    for (unsigned i = 0; i < 300; ++i) m.sent(i + 2, 105 + i, 3300, false, 0);
    check(m.pending() == 256 && m.overwritten == 44, "bounded storage under ACK loss");
    check(!m.observe(2, 3500) && m.observe(301, 3600), "old overwritten entry not falsely matched");
    accounting(m);
    const auto samples = m.observation_delay.count;
    m.sent(302, 406, 4000, false, 0);
    check(m.observe(302, 3900) && m.observation_delay.count == samples, "negative delay not aggregated");
    for (unsigned i = 303; i < 100303; ++i) {
        m.sent(i, i, i, false, 0);
        check(m.observe(i, int64_t(i) + 5), "sustained matching without growing storage");
    }
    accounting(m);
    PeriodicMetrics clock(10);
    check(!clock.due(30000009) && clock.due(30000010), "exact 30 second report boundary");
    check(!clock.due(30000010), "no duplicate report");
    check(clock.due(300000000) && !clock.due(300000001), "long stall does not cause catch-up burst");
    check(!clock.due(1) && clock.due(30000001), "backwards clock rebases safely");
    PeriodicMetrics extreme((std::numeric_limits<int64_t>::max)() - 3);
    check(!extreme.due((std::numeric_limits<int64_t>::max)()), "no signed deadline overflow");
    std::puts("PASS: exact ACK correlation, skips/duplicates/wrap/reset, bounded storage, 100k sends and report cadence");
}

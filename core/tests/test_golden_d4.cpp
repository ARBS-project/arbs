// SPDX-License-Identifier: GPL-3.0-or-later
// D4 golden replay: protocol, scheduler, probe policy, jitter buffer, arrival stats, session stats (exact match).
#include <cstdint>
#include <memory>
#include <string>

#include "arbs/arrival_stats.h"
#include "arbs/jitter_buffer.h"
#include "arbs/probe_policy.h"
#include "arbs/protocol.h"
#include "arbs/scheduler.h"
#include "arbs/session_stats.h"
#include "golden.h"
#include "mini_test.h"

using namespace arbs;
using golden::b01;

namespace {

void requirePass(const golden::Stats& st) {
    mt::report().push_back(st.summary());
    for (const std::string& e : st.firstErrors) mt::report().push_back("    " + e);
    for (const std::string& e : st.dblDiffSamples) mt::report().push_back("    double differs (within tolerance unless counted overTol): " + e);
    CHECK_MSG(st.pass(), st.summary());
    CHECK_MSG(st.inLines > 0, st.file + " replayed nothing");
}

// ------------------------------------------------------------------ protocol.txt
struct ProtocolReplayer : golden::Replayer {
    void onCase(const std::string&) override {}
    static std::string frameOut(const protocol::Frame& f) { return "frame=" + golden::hex(f.data(), f.size()); }
    void onIn(const std::string& l) override {
        if (l.rfind("IN build ", 0) == 0) {
            const std::string what = l.substr(9, l.find(' ', 9) == std::string::npos ? std::string::npos : l.find(' ', 9) - 9);
            if (what == "getInfo") emit("OUT! i=0 " + frameOut(protocol::buildGetInfo()));
            else if (what == "gripRealtimeEnable") emit("OUT! i=0 " + frameOut(protocol::gripRealtimeEnable()));
            else if (what == "triggerEnableBoth") emit("OUT! i=0 " + frameOut(protocol::triggerEnableBoth()));
            else if (what == "neutralFrame") emit("OUT! i=0 " + frameOut(protocol::neutralFrame()));
            else if (what == "disablePair") {
                const auto d = protocol::disablePair();
                for (size_t i = 0; i < d.size(); i++) emit("OUT! i=" + std::to_string(i) + " " + frameOut(d[i]));
            } else if (what == "triggerSideEnable") {
                const int route = static_cast<int>(golden::kvInt(l, "route"));
                try {
                    const auto d = protocol::triggerSideEnable(route);
                    if (route == protocol::ROUTE_LEFT || route == protocol::ROUTE_RIGHT) {
                        for (size_t i = 0; i < d.size(); i++) emit("OUT! i=" + std::to_string(i) + " " + frameOut(d[i]));
                    } else {
                        emit("OUT! exc=none");
                    }
                } catch (const std::invalid_argument&) {
                    emit("OUT! exc=IllegalArgumentException");
                }
            }
        } else if (l.rfind("IN frame ", 0) == 0) {
            const std::vector<int> lv = golden::kvInts(l, "l"), rv = golden::kvInts(l, "r");
            const bool hasT = golden::hasKey(l, "t");
            const std::vector<int> tv = hasT ? golden::kvInts(l, "t") : std::vector<int>();
            const bool on = golden::kvInt(l, "on") != 0;
            const int route = static_cast<int>(golden::kvInt(l, "route"));
            try {
                protocol::Frame f;
                if (!on && !hasT && route == protocol::ROUTE_MUTE) f = protocol::gripFrame(lv.data(), lv.size(), rv.data(), rv.size());
                else f = protocol::realtimeFrame(lv.data(), lv.size(), rv.data(), rv.size(), hasT ? tv.data() : nullptr, tv.size(), on, route);
                emit("OUT! " + frameOut(f));
            } catch (const std::invalid_argument&) {
                emit("OUT! exc=IllegalArgumentException");
            }
        } else if (l.rfind("IN parse ", 0) == 0) {
            const std::vector<uint8_t> raw = golden::hexBytes(golden::kv(l, "raw"));
            const size_t len = static_cast<size_t>(golden::kvInt(l, "len"));
            const auto in = protocol::parse(raw.data(), len);
            if (!in) emit("OUT! info=null");
            else
                emit("OUT! info dt=" + std::to_string(in->deviceType) + " cm=" + std::to_string(in->connectionMode) +
                     " bat=" + std::to_string(in->battery) + " feat=" + std::to_string(in->features) + " ck=" + b01(in->checksumOk) +
                     " pro=" + b01(in->isPro()) + " grip=" + b01(in->hasGripHaptics()));
        } else if (l.rfind("IN sendable ", 0) == 0) {
            const std::string raw = golden::kv(l, "raw");
            if (raw == "null") {
                emit(std::string("OUT! v=") + b01(protocol::isSendable(nullptr, 0)));
            } else {
                const std::vector<uint8_t> b = golden::hexBytes(raw);
                emit(std::string("OUT! v=") + b01(protocol::isSendable(b.data(), b.size())));
            }
        } else if (l.rfind("IN checksum ", 0) == 0) {
            const std::vector<uint8_t> b = golden::hexBytes(golden::kv(l, "raw"));
            emit("OUT! v=" + std::to_string(protocol::checksum(b.data())));
        } else {
            throw std::runtime_error("unknown IN: " + l.substr(0, 40));
        }
    }
};

// ------------------------------------------------------------------ scheduler.txt
struct SchedulerReplayer : golden::Replayer {
    Scheduler s;
    Scheduler::Decision d;
    void onCase(const std::string&) override { s = Scheduler(); }
    void onIn(const std::string& l) override {
        if (l.rfind("IN start ", 0) == 0) {
            s.start(golden::kvInt(l, "t"));
            emit("OUT! target=" + std::to_string(s.target()));
        } else if (l.rfind("IN poll ", 0) == 0) {
            s.poll(golden::kvInt(l, "t"), d);
            emit("OUT! wait=" + std::to_string(d.waitNs) + " skipped=" + std::to_string(d.skipped) + " catch=" + b01(d.catchingUp) +
                 " target=" + std::to_string(s.target()));
        } else if (l == "IN consume") {
            s.consume();
            emit("OUT! target=" + std::to_string(s.target()));
        } else if (l == "IN counters") {
            const int64_t m = s.getAndResetMissed(), r = s.getAndResetResyncs(), c = s.getAndResetCatchups();
            emit("OUT! missed=" + std::to_string(m) + " resyncs=" + std::to_string(r) + " catchups=" + std::to_string(c));
        } else {
            throw std::runtime_error("unknown IN");
        }
    }
};

// ------------------------------------------------------------------ probe.txt
struct ProbeReplayer : golden::Replayer {
    std::unique_ptr<ProbePolicy> p;
    void onCase(const std::string&) override { p = std::make_unique<ProbePolicy>(); }
    void onIn(const std::string& l) override {
        const std::string op = l.substr(3, l.find(' ', 3) == std::string::npos ? std::string::npos : l.find(' ', 3) - 3);
        std::string ret;
        if (op == "begin") { p->begin(); ret = "-"; }
        else if (op == "resetAll") { p->resetAll(); ret = "-"; }
        else if (op == "next") ret = std::to_string(p->nextAttempt());
        else if (op == "fail") ret = b01(p->onAttemptFailed());
        else if (op == "definitive") { p->onDefinitiveUnsupported(); ret = "-"; }
        else if (op == "ready") { p->onReady(); ret = "-"; }
        else if (op == "manual") ret = b01(p->manualRetry());
        else if (op == "input") ret = b01(p->onInput(golden::kvInt(l, "ms")));
        else throw std::runtime_error("unknown op " + op);
        emit("OUT! ret=" + ret + " attempt=" + std::to_string(p->attempt()) + " uns=" + b01(p->isUnsupported()) +
             " ready=" + b01(p->isReady()) + " seen=" + b01(p->inputSeen()));
    }
};

// ------------------------------------------------------------------ jitter.txt
struct JitterReplayer : golden::Replayer {
    JitterBuffer j;
    JitterBuffer::Result res;
    void onCase(const std::string&) override { j = JitterBuffer(); }
    void onIn(const std::string& l) override {
        if (l.rfind("IN fixed ", 0) == 0) {
            j.setFixed(static_cast<int>(golden::kvInt(l, "ms")));
            emit("OUT! fixed=" + std::to_string(j.getFixed()) + " target=" + std::to_string(j.getTarget()));
        } else if (l.rfind("IN reset ", 0) == 0) {
            j.reset(golden::kvInt(l, "t"));
        } else if (l.rfind("IN decide ", 0) == 0) {
            j.decide(static_cast<int>(golden::kvInt(l, "q")), golden::kvInt(l, "t"), res);
            emit(std::string("OUT! play=") + b01(res.play) + " trim=" + std::to_string(res.trimTo) + " ur=" + b01(res.underrun) +
                 " target=" + std::to_string(j.getTarget()) + " playing=" + b01(j.isPlaying()));
        } else if (l.rfind("IN adapt ", 0) == 0) {
            j.adaptFromP95(golden::kvDoubleBits(l, "p95"), static_cast<int>(golden::kvInt(l, "packets")), golden::kvInt(l, "t"));
            emit("OUT! target=" + std::to_string(j.getTarget()));
        } else if (l == "IN counters") {
            const int64_t u = j.getAndResetUnderruns();
            const int64_t t = j.getAndResetTrims();
            emit("OUT! underruns=" + std::to_string(u) + " trims=" + std::to_string(t));
        } else {
            throw std::runtime_error("unknown IN");
        }
    }
};

// ------------------------------------------------------------------ arrival.txt
struct ArrivalReplayer : golden::Replayer {
    std::unique_ptr<ArrivalStats> a;
    void onCase(const std::string&) override { a = std::make_unique<ArrivalStats>(); }
    void onIn(const std::string& l) override {
        if (l.rfind("IN add ", 0) == 0) {
            a->add(golden::kvInt(l, "t"));
        } else if (l == "IN reset") {
            a->reset();
        } else if (l.rfind("IN snap ", 0) == 0) {
            ArrivalStats::Snapshot s;
            a->snapshotAndReset(golden::kvInt(l, "t"), s);
            emit("OUT! packets=" + std::to_string(s.packets));
            emit("OUT~d avg=" + golden::db(s.avgMs) + " p95=" + golden::db(s.p95Ms) + " max=" + golden::db(s.maxMs) +
                 " sil=" + golden::db(s.maxSilenceMs));
        } else {
            throw std::runtime_error("unknown IN");
        }
    }
};

// ------------------------------------------------------------------ session_stats.txt
// Numeric tokens like Java Pattern "\d+\.\d+|\d+" (ASCII digits only; UTF-8 Korean bytes are >= 0x80).
std::string tokens(const std::string& s) {
    std::string out;
    size_t i = 0;
    auto dig = [](char c) { return c >= '0' && c <= '9'; };
    while (i < s.size()) {
        if (!dig(s[i])) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < s.size() && dig(s[j])) j++;
        if (j + 1 < s.size() && s[j] == '.' && dig(s[j + 1])) {
            j++;
            while (j < s.size() && dig(s[j])) j++;
        }
        if (!out.empty()) out.push_back(',');
        out += s.substr(i, j - i);
        i = j;
    }
    return out;
}

struct SessionReplayer : golden::Replayer {
    std::unique_ptr<SessionStats> s;
    void onCase(const std::string&) override { s = std::make_unique<SessionStats>(); }
    void get() {
        std::string v;
        for (int i = 0; i < SessionStats::N; i++) {
            if (i > 0) v.push_back(',');
            v += std::to_string(s->get(i));
        }
        emit("OUT! cum=" + v);
    }
    void onIn(const std::string& l) override {
        if (l.rfind("IN persec ", 0) == 0) {
            emit(std::string("OUT! v=") + b01(SessionStats::perSecondAllowed(golden::kvInt(l, "bytes"), golden::kvInt(l, "verbose") != 0)));
        } else if (l.rfind("IN due ", 0) == 0) {
            emit(std::string("OUT! v=") + b01(s->minuteDue(golden::kvInt(l, "t"))));
        } else if (l.rfind("IN add ", 0) == 0) {
            s->add(static_cast<int>(golden::kvInt(l, "i")), golden::kvInt(l, "v"));
        } else if (l.rfind("IN set ", 0) == 0) {
            s->set(static_cast<int>(golden::kvInt(l, "i")), golden::kvInt(l, "v"));
        } else if (l.rfind("IN reset ", 0) == 0) {
            s->reset(golden::kvInt(l, "t"));
        } else if (l == "IN get") {
            get();
        } else if (l.rfind("IN batstart ", 0) == 0) {
            s->batteryStart(static_cast<int>(golden::kvInt(l, "v")));
        } else if (l.rfind("IN batend ", 0) == 0) {
            s->batteryEnd(static_cast<int>(golden::kvInt(l, "v")));
        } else if (l == "IN crash") {
            s->markCrash();
        } else if (l.rfind("IN silence ", 0) == 0) {
            s->silenceMs(golden::kvDoubleBits(l, "ms"));
        } else if (l.rfind("IN extra ", 0) == 0) {
            s->addExtraDelay(golden::kvDoubleBits(l, "ms"));
        } else if (l.rfind("IN rtt ", 0) == 0) {
            s->addRtt(golden::kvDoubleBits(l, "ms"));
        } else if (l.rfind("IN minute ", 0) == 0) {
            emit("OUT! tokens=" + tokens(s->minuteLine(golden::kvInt(l, "t"), "S", "A")));
        } else if (l.rfind("IN session ", 0) == 0) {
            emit("OUT! tokens=" + tokens(s->sessionLine(golden::kvInt(l, "t"), "R")));
        } else {
            throw std::runtime_error("unknown IN");
        }
    }
};

golden::Policy exact() {
    golden::Policy p;
    p.intTol = 0;
    return p;
}

}  // namespace

TEST(golden_d4, protocol) {
    ProtocolReplayer r;
    requirePass(golden::run("protocol.txt", r, exact(), "exact"));
}

TEST(golden_d4, scheduler) {
    SchedulerReplayer r;
    requirePass(golden::run("scheduler.txt", r, exact(), "exact"));
}

TEST(golden_d4, probe) {
    ProbeReplayer r;
    requirePass(golden::run("probe.txt", r, exact(), "exact"));
}

TEST(golden_d4, jitter) {
    JitterReplayer r;
    requirePass(golden::run("jitter.txt", r, exact(), "exact"));
}

TEST(golden_d4, arrival) {
    ArrivalReplayer r;
    requirePass(golden::run("arrival.txt", r, exact(), "exact"));
}

TEST(golden_d4, session_stats) {
    SessionReplayer r;
    requirePass(golden::run("session_stats.txt", r, exact(), "exact"));
}

// Golden files must be unchanged (MANIFEST.txt SHA-256 per file).
TEST(golden_d4, manifest_unchanged) {
    const std::vector<std::string> lines = golden::readLines("MANIFEST.txt");
    int files = 0;
    for (const std::string& l : lines) {
        if (l.empty() || l[0] == '#' || l.find('=') != std::string::npos) continue;
        const size_t a = l.find(' ');
        const size_t b = l.find(' ', a + 1);
        const std::string name = l.substr(0, a), sha = l.substr(a + 1, b - a - 1);
        const std::string got = golden::sha256File(mt::goldenDir() + "/" + name);
        CHECK_MSG(got == sha, name + " sha256 " + got + " != MANIFEST " + sha);
        files++;
    }
    CHECK_EQ(files, 19);
    mt::report().push_back("MANIFEST.txt: " + std::to_string(files) + " golden files, SHA-256 all equal to MANIFEST");
}

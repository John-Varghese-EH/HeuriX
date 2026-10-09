/**
 * HeuriX Unit Test Suite
 * Uses doctest (header-only, no external dependencies).
 *
 * Build:
 *   cmake -DBUILD_TESTS=ON -B build_tests && cmake --build build_tests -t heurix-tests
 * Run:
 *   ./build_tests/heurix-tests
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "heurix/entropy.hpp"
#include "heurix/features.hpp"
#include "heurix/ml_model.hpp"
#include "heurix/types.hpp"
#include "heurix/heuristic_engine.hpp"
#include "heurix/event_bus.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <span>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// Helper utilities
// ============================================================================

static uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// RAII temporary file helper
struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& suffix = ".tmp") {
        path = std::filesystem::temp_directory_path() /
               ("heurix_test_" + std::to_string(now_ms()) + suffix);
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    void write(const std::vector<uint8_t>& data) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    void write_str(const std::string& s) {
        std::ofstream f(path);
        f << s;
    }
};

// RAII temporary directory
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("heurix_testdir_" + std::to_string(now_ms()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// ============================================================================
// Tests: Shannon Entropy
// ============================================================================

TEST_SUITE("Entropy") {

    TEST_CASE("empty data returns 0") {
        std::vector<uint8_t> data{};
        CHECK(shannon_entropy(std::span<const uint8_t>(data)) == doctest::Approx(0.0));
    }

    TEST_CASE("uniform single-byte data returns 0 entropy") {
        std::vector<uint8_t> data(1024, 0x41); // all 'A'
        double h = shannon_entropy(std::span<const uint8_t>(data));
        CHECK(h == doctest::Approx(0.0).epsilon(1e-9));
    }

    TEST_CASE("two-symbol data returns 1.0 bit") {
        std::vector<uint8_t> data;
        data.reserve(1024);
        for (size_t i = 0; i < 512; ++i) data.push_back(0x00);
        for (size_t i = 0; i < 512; ++i) data.push_back(0xFF);
        double h = shannon_entropy(std::span<const uint8_t>(data));
        CHECK(h == doctest::Approx(1.0).epsilon(1e-6));
    }

    TEST_CASE("fully uniform 256-symbol data returns exactly 8.0 bits") {
        std::vector<uint8_t> data(256 * 16);
        for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i % 256);
        double h = shannon_entropy(std::span<const uint8_t>(data));
        CHECK(h == doctest::Approx(8.0).epsilon(1e-6));
    }

    TEST_CASE("high entropy random-like data is close to 8 bits") {
        // Pseudo-random via LCG for determinism
        std::vector<uint8_t> data(4096);
        uint32_t state = 0xDEADBEEF;
        for (auto& b : data) { state = state * 1664525u + 1013904223u; b = state >> 24; }
        double h = shannon_entropy(std::span<const uint8_t>(data));
        CHECK(h > 7.9);
        CHECK(h <= 8.0);
    }

    TEST_CASE("ASCII text data has low entropy (< 5 bits)") {
        std::string text =
            "The quick brown fox jumps over the lazy dog. "
            "Pack my box with five dozen liquor jugs. "
            "How vile and base your ranks your ranks.";
        std::vector<uint8_t> data(text.begin(), text.end());
        double h = shannon_entropy(std::span<const uint8_t>(data));
        CHECK(h < 5.0);
        CHECK(h > 0.5);
    }

    TEST_CASE("single-byte data has 0 entropy") {
        std::vector<uint8_t> data{0x42};
        CHECK(shannon_entropy(std::span<const uint8_t>(data)) == doctest::Approx(0.0));
    }
}

// ============================================================================
// Tests: Feature Extraction
// ============================================================================

TEST_SUITE("FeatureExtractor") {

    TEST_CASE("modify event on a plain text file") {
        TempFile tf(".txt");
        tf.write_str("hello world, this is a plain text file for testing");

        FsEvent ev;
        ev.path = tf.path.string();
        ev.type = EventType::modify;
        ev.timestamp_ms = now_ms();

        FeatureExtractor extractor;
        auto f = extractor.extract(ev);

        CHECK(f[0] == doctest::Approx(0.0)); // modify = 0
        CHECK(f[1] > 0.0);                   // entropy > 0
        CHECK(f[1] < 6.0);                   // text should be low entropy
        CHECK(f[3] > 0.0);                   // log2_size > 0
        // .txt is classified as 'document' (ExtClass::document = 2)
        CHECK(f[4] == doctest::Approx(static_cast<double>(static_cast<int>(ExtClass::document))));
        CHECK(f[5] == doctest::Approx(0.0)); // no magic mismatch for plain text
    }

    TEST_CASE("delete event sets entropy to -1") {
        FsEvent ev;
        ev.path = "/nonexistent/file.pdf";
        ev.type = EventType::del;
        ev.timestamp_ms = now_ms();

        FeatureExtractor extractor;
        auto f = extractor.extract(ev);

        CHECK(f[0] == doctest::Approx(static_cast<double>(static_cast<int>(EventType::del))));
        CHECK(f[1] == doctest::Approx(-1.0)); // del: no content to read
    }

    TEST_CASE("rename event increments rename counter in window") {
        FeatureExtractor extractor;
        uint64_t ts = now_ms();

        for (int i = 0; i < 5; ++i) {
            FsEvent ev;
            ev.path = "/some/path/file_" + std::to_string(i) + ".locked";
            ev.type = EventType::rename;
            ev.timestamp_ms = ts + i * 10;
            extractor.extract(ev);
        }

        FsEvent last;
        last.path = "/some/path/file_final.locked";
        last.type = EventType::rename;
        last.timestamp_ms = ts + 50;
        auto f = extractor.extract(last);

        CHECK(f[7] >= 5.0); // renames_in_window
    }

    TEST_CASE("window slides out old events") {
        FeatureExtractor extractor;
        uint64_t ts = now_ms();

        // Add events far in the past (beyond the 2s window)
        for (int i = 0; i < 10; ++i) {
            FsEvent ev;
            ev.path = "/tmp/old_" + std::to_string(i) + ".txt";
            ev.type = EventType::modify;
            ev.timestamp_ms = ts - 5000; // 5 seconds ago
            extractor.extract(ev);
        }

        // Add a fresh event
        FsEvent fresh;
        fresh.path = "/tmp/fresh.txt";
        fresh.type = EventType::create;
        fresh.timestamp_ms = ts;
        auto f = extractor.extract(fresh);

        CHECK(f[6] == doctest::Approx(1.0)); // only the fresh event in window
    }

    TEST_CASE("high entropy file increments high_entropy_in_window counter") {
        TempFile tf(".bin");
        // Write high-entropy data
        std::vector<uint8_t> data(4096);
        uint32_t s = 0xCAFEBABE;
        for (auto& b : data) { s = s * 1664525u + 1013904223u; b = s >> 24; }
        tf.write(data);

        FeatureExtractor extractor;
        FsEvent ev;
        ev.path = tf.path.string();
        ev.type = EventType::modify;
        ev.timestamp_ms = now_ms();
        auto f = extractor.extract(ev);

        CHECK(f[1] > 7.5);   // high entropy
        CHECK(f[9] >= 1.0);  // high_entropy_in_window
    }

    TEST_CASE("extension classification covers all categories") {
        CHECK(classify_extension(".pdf")      == ExtClass::document);
        CHECK(classify_extension(".jpg")      == ExtClass::image);
        CHECK(classify_extension(".mp4")      == ExtClass::media);
        CHECK(classify_extension(".zip")      == ExtClass::archive);
        CHECK(classify_extension(".cpp")      == ExtClass::code_text);
        CHECK(classify_extension(".locked")   == ExtClass::known_ransom);
        CHECK(classify_extension(".wncry")    == ExtClass::known_ransom);
        CHECK(classify_extension(".xyz_junk") == ExtClass::other);
        CHECK(classify_extension("")          == ExtClass::none);
    }

    TEST_CASE("magic byte mismatch detected for fake PDF") {
        TempFile tf(".pdf");
        // Write random bytes instead of "%PDF"
        std::vector<uint8_t> data(64, 0xFF);
        tf.write(data);

        FeatureExtractor extractor;
        FsEvent ev;
        ev.path = tf.path.string();
        ev.type = EventType::modify;
        ev.timestamp_ms = now_ms();
        auto f = extractor.extract(ev);

        CHECK(f[5] == doctest::Approx(1.0)); // header mismatch
    }

    TEST_CASE("no magic mismatch for real PNG-like header") {
        TempFile tf(".png");
        std::vector<uint8_t> data = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        data.resize(64, 0x00);
        tf.write(data);

        FeatureExtractor extractor;
        FsEvent ev;
        ev.path = tf.path.string();
        ev.type = EventType::modify;
        ev.timestamp_ms = now_ms();
        auto f = extractor.extract(ev);

        CHECK(f[5] == doctest::Approx(0.0)); // no mismatch
    }
}

// ============================================================================
// Tests: ML Model (HXRF1)
// ============================================================================

TEST_SUITE("RandomForestModel") {

    // Build a minimal valid HXRF1 model file in-memory.
    static std::string make_minimal_model() {
        std::ostringstream m;
        m << "HXRF1\n";
        m << "schema " << kFeatureSchemaVersion << "\n";
        m << "features";
        for (auto name : kFeatureNames) m << " " << name;
        m << "\n";
        m << "trees 2\n";
        // Tree 0: single leaf that always predicts 0.9
        m << "tree 1\n";
        m << "-1 -1 -1 0.0 0.9\n";
        // Tree 1: single leaf that always predicts 0.8
        m << "tree 1\n";
        m << "-1 -1 -1 0.0 0.8\n";
        return m.str();
    }

    // Build a 3-node decision tree model (root + 2 leaves).
    static std::string make_decision_tree_model() {
        std::ostringstream m;
        m << "HXRF1\n";
        m << "schema " << kFeatureSchemaVersion << "\n";
        m << "features";
        for (auto name : kFeatureNames) m << " " << name;
        m << "\n";
        m << "trees 1\n";
        // Root (i=0): feature 1 (entropy) <= 5.0 -> left (i=1), right (i=2)
        m << "tree 3\n";
        m << "1 2 1 5.0 0.0\n";   // node 0: split on entropy at 5.0
        m << "-1 -1 -1 0.0 0.1\n"; // node 1: leaf, low risk
        m << "-1 -1 -1 0.0 0.9\n"; // node 2: leaf, high risk
        return m.str();
    }

    TEST_CASE("load returns error for missing file") {
        RandomForestModel mdl;
        auto err = mdl.load("/nonexistent/path/model.hxrf1");
        CHECK(!err.empty());
        CHECK(!mdl.loaded());
    }

    TEST_CASE("load returns error for wrong magic") {
        TempFile tf(".hxrf1");
        tf.write_str("INVALID_MAGIC\nschema 1\n");
        RandomForestModel mdl;
        auto err = mdl.load(tf.path.string());
        CHECK(!err.empty());
    }

    TEST_CASE("load succeeds for valid minimal model") {
        TempFile tf(".hxrf1");
        tf.write_str(make_minimal_model());
        RandomForestModel mdl;
        auto err = mdl.load(tf.path.string());
        CHECK(err.empty());
        CHECK(mdl.loaded());
    }

    TEST_CASE("predict returns mean of leaf values for leaf-only model") {
        TempFile tf(".hxrf1");
        tf.write_str(make_minimal_model());
        RandomForestModel mdl;
        mdl.load(tf.path.string());

        FeatureVector f{};
        double p = mdl.predict(f);
        CHECK(p == doctest::Approx(0.85).epsilon(1e-6)); // mean(0.9, 0.8)
    }

    TEST_CASE("predict routes correctly through decision tree") {
        TempFile tf(".hxrf1");
        tf.write_str(make_decision_tree_model());
        RandomForestModel mdl;
        mdl.load(tf.path.string());

        FeatureVector low{};
        low[1] = 3.0; // entropy = 3.0 -> left leaf -> 0.1
        CHECK(mdl.predict(low) == doctest::Approx(0.1).epsilon(1e-6));

        FeatureVector high{};
        high[1] = 7.8; // entropy = 7.8 -> right leaf -> 0.9
        CHECK(mdl.predict(high) == doctest::Approx(0.9).epsilon(1e-6));
    }

    TEST_CASE("predict on empty model returns 0.0") {
        RandomForestModel mdl;
        FeatureVector f{};
        CHECK(mdl.predict(f) == doctest::Approx(0.0));
    }

    TEST_CASE("load rejects model with wrong schema version") {
        std::ostringstream m;
        m << "HXRF1\nschema 999\nfeatures";
        for (auto n : kFeatureNames) m << " " << n;
        m << "\ntrees 1\ntree 1\n-1 -1 -1 0.0 0.5\n";
        TempFile tf(".hxrf1");
        tf.write_str(m.str());
        RandomForestModel mdl;
        auto err = mdl.load(tf.path.string());
        CHECK(!err.empty()); // schema mismatch
    }

    TEST_CASE("load rejects model with bad child indices (cycle guard)") {
        std::ostringstream m;
        m << "HXRF1\nschema " << kFeatureSchemaVersion << "\nfeatures";
        for (auto n : kFeatureNames) m << " " << n;
        // Tree with node pointing backwards (would cause infinite loop without validation).
        m << "\ntrees 1\ntree 2\n";
        m << "0 1 1 5.0 0.0\n"; // left=0 == self: invalid
        m << "-1 -1 -1 0.0 0.5\n";
        TempFile tf(".hxrf1");
        tf.write_str(m.str());
        RandomForestModel mdl;
        auto err = mdl.load(tf.path.string());
        CHECK(!err.empty()); // must reject self-referential node
    }
}

// ============================================================================
// Tests: Heuristic Engine
// ============================================================================

TEST_SUITE("HeuristicEngine") {

    static EngineConfig default_config() {
        EngineConfig cfg;
        cfg.watch_dir = "/tmp/heurix_test_canary";
        cfg.entropy_threshold = 7.5;
        cfg.burst_count = 5;
        cfg.burst_window_ms = 2000;
        cfg.burst_rename_count = 3;
        cfg.entropy_cascade_count = 3;
        cfg.auto_mitigate = false;
        cfg.auto_kill = false;
        cfg.enable_ml = false;
        return cfg;
    }

    TEST_CASE("no alert on benign low-entropy modification") {
        HeuristicEngine engine(default_config());
        TempFile tf(".txt");
        tf.write_str("hello world");

        FsEvent ev;
        ev.path = tf.path.string();
        ev.type = EventType::modify;
        ev.timestamp_ms = now_ms();

        auto alert = engine.analyze(ev);
        CHECK(!alert.has_value());
    }

    TEST_CASE("high entropy file triggers alert") {
        HeuristicEngine engine(default_config());
        TempFile tf(".dat");
        // Write high-entropy data
        std::vector<uint8_t> data(4096);
        uint32_t s = 0x12345678;
        for (auto& b : data) { s = s * 1664525u + 1013904223u; b = s >> 24; }
        tf.write(data);

        FsEvent ev;
        ev.path = tf.path.string();
        ev.type = EventType::modify;
        ev.timestamp_ms = now_ms();

        auto alert = engine.analyze(ev);
        REQUIRE(alert.has_value());
        CHECK(alert->entropy > 7.0);
        CHECK(alert->severity >= Severity::high);
    }

    TEST_CASE("burst of events triggers medium-severity alert") {
        HeuristicEngine engine(default_config());
        uint64_t ts = now_ms();

        TempFile tf(".dat");
        tf.write_str("data");

        std::optional<Alert> last_alert;
        for (int i = 0; i <= 6; ++i) {
            FsEvent ev;
            ev.path = tf.path.string() + "_" + std::to_string(i);
            ev.type = EventType::modify;
            ev.timestamp_ms = ts + i * 50;
            last_alert = engine.analyze(ev);
        }
        // After 6 events (> burst_count=5) in 300ms (< 2000ms window)
        REQUIRE(last_alert.has_value());
        CHECK(last_alert->severity >= Severity::medium);
    }

    TEST_CASE("known ransomware extension triggers high alert") {
        HeuristicEngine engine(default_config());
        FsEvent ev;
        ev.path = "/tmp/invoice.docx.locked";
        ev.type = EventType::create;
        ev.timestamp_ms = now_ms();

        auto alert = engine.analyze(ev);
        REQUIRE(alert.has_value());
        CHECK(alert->severity >= Severity::high);
    }

    TEST_CASE("mass rename triggers high alert") {
        HeuristicEngine engine(default_config());
        uint64_t ts = now_ms();

        std::optional<Alert> last_alert;
        for (int i = 0; i < 4; ++i) {
            FsEvent ev;
            ev.path = "/tmp/file_" + std::to_string(i) + ".wncry";
            ev.type = EventType::rename;
            ev.timestamp_ms = ts + i * 20;
            last_alert = engine.analyze(ev);
        }
        REQUIRE(last_alert.has_value());
        CHECK(last_alert->severity >= Severity::high);
    }

    TEST_CASE("entropy cascade triggers critical alert") {
        HeuristicEngine engine(default_config());
        uint64_t ts = now_ms();

        // Write 3 high-entropy files to trigger cascade (cascade_count=3)
        std::vector<TempFile> files;
        for (int i = 0; i < 3; ++i) {
            files.emplace_back(".bin");
            std::vector<uint8_t> data(4096);
            uint32_t s = 0xABCD0000 + i;
            for (auto& b : data) { s = s * 1664525u + 1013904223u; b = s >> 24; }
            files.back().write(data);
        }

        std::optional<Alert> last_alert;
        for (int i = 0; i < 3; ++i) {
            FsEvent ev;
            ev.path = files[i].path.string();
            ev.type = EventType::modify;
            ev.timestamp_ms = ts + i * 100;
            last_alert = engine.analyze(ev);
        }
        REQUIRE(last_alert.has_value());
        CHECK(last_alert->severity == Severity::critical);
    }

    TEST_CASE("safelisted path does not generate alerts from burst") {
        EngineConfig cfg = default_config();
        cfg.safelist = {"/tmp/safe_"};
        HeuristicEngine engine(cfg);

        uint64_t ts = now_ms();
        std::optional<Alert> last_alert;
        for (int i = 0; i <= 10; ++i) {
            FsEvent ev;
            ev.path = "/tmp/safe_file_" + std::to_string(i) + ".cpp";
            ev.type = EventType::modify;
            ev.timestamp_ms = ts + i * 50;
            last_alert = engine.analyze(ev);
        }
        CHECK(!last_alert.has_value());
    }

    TEST_CASE("canary modification triggers critical alert") {
        TempDir dir;
        EngineConfig cfg = default_config();
        cfg.watch_dir = dir.path.string();
        HeuristicEngine engine(cfg);
        engine.deploy_canaries();

        auto& canary_paths = engine.get_canary_paths();
        if (!canary_paths.empty()) {
            const std::string& cp = *canary_paths.begin();
            // Register canary
            FsEvent ev;
            ev.path = cp;
            ev.type = EventType::modify;
            ev.timestamp_ms = now_ms();
            auto alert = engine.analyze(ev);
            REQUIRE(alert.has_value());
            CHECK(alert->severity == Severity::critical);
        }
    }

    TEST_CASE("verify_canaries returns nullopt when files are intact") {
        TempDir dir;
        EngineConfig cfg = default_config();
        cfg.watch_dir = dir.path.string();
        HeuristicEngine engine(cfg);
        engine.deploy_canaries();

        // Files should be intact immediately after deployment
        auto alert = engine.verify_canaries();
        CHECK(!alert.has_value());
    }

    TEST_CASE("verify_canaries detects tampered canary") {
        TempDir dir;
        EngineConfig cfg = default_config();
        cfg.watch_dir = dir.path.string();
        HeuristicEngine engine(cfg);
        engine.deploy_canaries();

        auto& paths = engine.get_canary_paths();
        if (!paths.empty()) {
            // Tamper the first canary
            std::ofstream f(*paths.begin(), std::ios::trunc);
            f << "ransomware_was_here";
            f.close();

            auto alert = engine.verify_canaries();
            REQUIRE(alert.has_value());
            CHECK(alert->severity == Severity::critical);
        }
    }

    TEST_CASE("current_threat_score increases on threats") {
        HeuristicEngine engine(default_config());
        CHECK(engine.current_threat_score() == doctest::Approx(0.0));

        FsEvent ev;
        ev.path = "/tmp/evil.locked";
        ev.type = EventType::create;
        ev.timestamp_ms = now_ms();
        engine.analyze(ev);

        CHECK(engine.current_threat_score() > 0.0);
    }

    TEST_CASE("update_config changes thresholds live") {
        HeuristicEngine engine(default_config());

        // Set entropy threshold very low so even benign text triggers it.
        EngineConfig cfg = default_config();
        cfg.entropy_threshold = 0.1; // trigger on anything
        engine.update_config(cfg);

        TempFile tf(".txt");
        tf.write_str("abcdefghijklmnopqrstuvwxyz");

        FsEvent ev;
        ev.path = tf.path.string();
        ev.type = EventType::modify;
        ev.timestamp_ms = now_ms();

        auto alert = engine.analyze(ev);
        REQUIRE(alert.has_value()); // should trigger now
    }
}

// ============================================================================
// Tests: EventBus
// ============================================================================

TEST_SUITE("EventBus") {

    TEST_CASE("subscribe and receive a FsEvent") {
        auto sub = EventBus::instance().subscribe(16);

        FsEvent ev;
        ev.path = "/test/path.txt";
        ev.type = EventType::modify;
        ev.timestamp_ms = now_ms();

        emit_event(ev);

        auto msg = sub->pop(std::chrono::milliseconds(500));
        REQUIRE(msg.has_value());
        REQUIRE(std::holds_alternative<FsEvent>(*msg));
        CHECK(std::get<FsEvent>(*msg).path == "/test/path.txt");

        EventBus::instance().unsubscribe(sub);
    }

    TEST_CASE("subscribe and receive an Alert") {
        auto sub = EventBus::instance().subscribe(16);

        Alert alert;
        alert.severity = Severity::critical;
        alert.description = "test alert";
        alert.threat_score = 99.0;
        alert.timestamp_ms = now_ms();

        emit_alert(alert);

        auto msg = sub->pop(std::chrono::milliseconds(500));
        REQUIRE(msg.has_value());
        REQUIRE(std::holds_alternative<Alert>(*msg));
        CHECK(std::get<Alert>(*msg).severity == Severity::critical);
        CHECK(std::get<Alert>(*msg).threat_score == doctest::Approx(99.0));

        EventBus::instance().unsubscribe(sub);
    }

    TEST_CASE("pop returns nullopt on timeout when no messages") {
        auto sub = EventBus::instance().subscribe(4);
        auto msg = sub->pop(std::chrono::milliseconds(50));
        CHECK(!msg.has_value());
        EventBus::instance().unsubscribe(sub);
    }

    TEST_CASE("closed subscription returns nullopt") {
        auto sub = EventBus::instance().subscribe(4);
        EventBus::instance().unsubscribe(sub);
        auto msg = sub->pop(std::chrono::milliseconds(50));
        CHECK(!msg.has_value());
    }

    TEST_CASE("capacity cap: alerts prioritized over events on overflow") {
        // Small capacity to force overflow
        auto sub = EventBus::instance().subscribe(4);

        uint64_t ts = now_ms();
        // Fill the queue with FsEvents
        for (int i = 0; i < 8; ++i) {
            FsEvent ev;
            ev.path = "/overflow_" + std::to_string(i);
            ev.type = EventType::modify;
            ev.timestamp_ms = ts + i;
            EventBus::instance().publish(ev);
        }
        // Now publish a critical alert - should displace a non-alert
        Alert alert;
        alert.severity = Severity::critical;
        alert.description = "priority alert";
        alert.timestamp_ms = ts + 100;
        EventBus::instance().publish(alert);

        // Drain all messages; alert must survive
        bool found_alert = false;
        for (int i = 0; i < 10; ++i) {
            auto msg = sub->pop(std::chrono::milliseconds(10));
            if (!msg) break;
            if (std::holds_alternative<Alert>(*msg)) found_alert = true;
        }
        CHECK(found_alert);
        EventBus::instance().unsubscribe(sub);
    }

    TEST_CASE("multiple subscribers each receive messages") {
        auto s1 = EventBus::instance().subscribe(8);
        auto s2 = EventBus::instance().subscribe(8);

        FsEvent ev;
        ev.path = "/multi/test";
        ev.type = EventType::create;
        ev.timestamp_ms = now_ms();
        emit_event(ev);

        auto m1 = s1->pop(std::chrono::milliseconds(200));
        auto m2 = s2->pop(std::chrono::milliseconds(200));

        CHECK(m1.has_value());
        CHECK(m2.has_value());

        EventBus::instance().unsubscribe(s1);
        EventBus::instance().unsubscribe(s2);
    }
}

// ============================================================================
// Tests: Type serialization
// ============================================================================

TEST_SUITE("TypeSerialization") {

    TEST_CASE("FsEvent to_json produces valid JSON structure") {
        FsEvent ev;
        ev.path = "/some/path with \"quotes\".txt";
        ev.type = EventType::modify;
        ev.timestamp_ms = 1728451200000ULL;
        std::string j = ev.to_json();
        CHECK(j.find("\"path\"") != std::string::npos);
        CHECK(j.find("\\\"quotes\\\"") != std::string::npos); // escaped
        CHECK(j.find("\"modify\"") != std::string::npos);
        CHECK(j.find("1728451200000") != std::string::npos);
    }

    TEST_CASE("Alert to_json includes all fields") {
        Alert a;
        a.severity = Severity::critical;
        a.description = "test\\ndesc";
        a.entropy = 7.92;
        a.pid = 1234;
        a.process_name = "evil.exe";
        a.action = MitigationAction::terminated_tree;
        a.quarantine_path = "/q/file";
        a.killed_pids = {1234, 1235};
        a.threat_score = 98.5;
        a.timestamp_ms = 100;

        std::string j = a.to_json();
        CHECK(j.find("\"critical\"") != std::string::npos);
        CHECK(j.find("7.92") != std::string::npos);
        CHECK(j.find("1234") != std::string::npos);
        CHECK(j.find("evil.exe") != std::string::npos);
        CHECK(j.find("terminated_tree") != std::string::npos);
        CHECK(j.find("/q/file") != std::string::npos);
        CHECK(j.find("1235") != std::string::npos);
    }

    TEST_CASE("severity_str returns correct strings") {
        CHECK(severity_str(Severity::info)     == "info");
        CHECK(severity_str(Severity::low)      == "low");
        CHECK(severity_str(Severity::medium)   == "medium");
        CHECK(severity_str(Severity::high)     == "high");
        CHECK(severity_str(Severity::critical) == "critical");
    }

    TEST_CASE("action_str returns correct strings") {
        CHECK(action_str(MitigationAction::none)            == "none");
        CHECK(action_str(MitigationAction::suspended)       == "suspended");
        CHECK(action_str(MitigationAction::terminated)      == "terminated");
        CHECK(action_str(MitigationAction::quarantined)     == "quarantined");
        CHECK(action_str(MitigationAction::terminated_tree) == "terminated_tree");
    }
}

// ============================================================================
// Tests: FeatureLogger
// ============================================================================

TEST_SUITE("FeatureLogger") {

    TEST_CASE("writes header and data row to CSV") {
        TempFile tf(".csv");
        {
            // Scope to ensure logger is destroyed (flushes & closes) before we read the file.
            FeatureLogger logger;
            bool ok = logger.open(tf.path.string(), "benign");
            REQUIRE(ok);
            CHECK(logger.is_open());

            FeatureVector f{};
            for (size_t i = 0; i < kNumFeatures; ++i) f[i] = static_cast<double>(i) * 0.1;
            logger.write(12345678ULL, f);
        } // logger destroyed here → file closed & flushed

        // Read back and verify
        std::ifstream in(tf.path);
        REQUIRE(in.is_open());
        std::string line1, line2, line3;
        std::getline(in, line1); // schema comment
        std::getline(in, line2); // header
        std::getline(in, line3); // data row

        CHECK(line1.find("heurix_feature_schema") != std::string::npos);
        CHECK(line2.find("timestamp_ms") != std::string::npos);
        CHECK(line2.find("event_type") != std::string::npos);
        CHECK(line2.find("label") != std::string::npos);
        CHECK(line3.find("12345678") != std::string::npos);
        CHECK(line3.find("benign") != std::string::npos);
    }

    TEST_CASE("appends to existing CSV without duplicate header") {
        TempFile tf(".csv");
        {
            FeatureLogger logger;
            logger.open(tf.path.string(), "malicious");
            FeatureVector f{}; logger.write(1ULL, f);
        }
        {
            FeatureLogger logger;
            logger.open(tf.path.string(), "benign");
            FeatureVector f{}; logger.write(2ULL, f);
        }
        // Count header lines (should be exactly 1 comment + 1 header)
        std::ifstream in(tf.path);
        int header_count = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("timestamp_ms") != std::string::npos) ++header_count;
        }
        CHECK(header_count == 1);
    }
}

#include <gtest/gtest.h>

#include <QSemaphore>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "engine/sidechain/enginesidechain.h"
#include "engine/sidechain/sidechainworker.h"
#include "test/mixxxtest.h"

namespace {

struct WorkerState {
    QSemaphore processingStarted;
    QSemaphore finishProcessing;
    std::vector<CSAMPLE> samples;
    int shutdownCalls = 0;
    bool destroyed = false;
};

class BlockingSideChainWorker : public SideChainWorker {
  public:
    explicit BlockingSideChainWorker(WorkerState* state)
            : m_state(state) {
    }

    ~BlockingSideChainWorker() override {
        m_state->destroyed = true;
    }

    void process(const CSAMPLE* buffer, std::size_t size) override {
        m_state->samples.insert(m_state->samples.end(), buffer, buffer + size);
        m_state->processingStarted.release();
        m_state->finishProcessing.acquire();
    }

    void shutdown() override {
        ++m_state->shutdownCalls;
    }

  private:
    WorkerState* m_state;
};

class EngineSideChainTest : public MixxxTest {};

TEST_F(EngineSideChainTest, ShutdownWithoutSamples) {
    std::array<CSAMPLE, 2> sidechainMix{};
    EngineSideChain sidechain(config(), sidechainMix.data());
}

TEST_F(EngineSideChainTest, ShutdownDrainsPendingSamplesBeforeWorkerShutdown) {
    WorkerState state;
    std::array<CSAMPLE, 1024> samples;
    samples.fill(0.25f);
    auto sidechain = std::make_unique<EngineSideChain>(config(), samples.data());
    sidechain->addSideChainWorker(new BlockingSideChainWorker(&state));
    sidechain->writeSamples(samples.data(), static_cast<int>(samples.size() / 2));

    std::atomic<bool> shutdownFinished{false};
    std::thread shutdown([&] {
        sidechain.reset();
        shutdownFinished.store(true);
    });

    const bool processingStarted = state.processingStarted.tryAcquire(1, 5000);
    EXPECT_TRUE(processingStarted);
    if (processingStarted) {
        EXPECT_FALSE(shutdownFinished.load());
    }
    state.finishProcessing.release();
    shutdown.join();

    EXPECT_EQ(state.samples, std::vector<CSAMPLE>(samples.begin(), samples.end()));
    EXPECT_EQ(state.shutdownCalls, 1);
    EXPECT_TRUE(state.destroyed);
}

} // namespace

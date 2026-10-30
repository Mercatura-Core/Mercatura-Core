// Copyright (c) 2026 The Mercatura developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <node/blockstorage.h>
#include <node/miner.h>
#include <pow.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <test/util/sparse.h>
#include <txdb.h>
#include <undo.h>
#include <util/fs_helpers.h>
#include <validation.h>
#include <validationinterface.h>

#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32
#include <sys/stat.h>
#endif

namespace {
struct CommitControl {
    std::string fail_file;
    unsigned failures{0};
    unsigned calls{0};

    bool Commit(FILE* file, const fs::path& path)
    {
        ++calls;
        if (fs::PathToString(path.filename()) == fail_file) {
            ++failures;
            return false;
        }
        return FileCommit(file);
    }
};

struct RawBytes {
    std::vector<std::byte> bytes;
    template <typename Stream> void Unserialize(Stream& stream)
    {
        bytes.resize(stream.size());
        stream.read(bytes);
    }
};
using DBSnapshot = std::vector<std::pair<std::vector<std::byte>, std::vector<std::byte>>>;

DBSnapshot Snapshot(CDBWrapper& db)
{
    DBSnapshot result;
    std::unique_ptr<CDBIterator> it{db.NewIterator()};
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        RawBytes key, value;
        BOOST_REQUIRE(it->GetKey(key));
        BOOST_REQUIRE(it->GetValue(value));
        result.emplace_back(std::move(key.bytes), std::move(value.bytes));
    }
    return result;
}

struct FlushSubscriber final : CValidationInterface {
    unsigned count{0};
    void ChainStateFlushed(const kernel::ChainstateRole&, const CBlockLocator&) override { ++count; }
};

struct DurabilitySetup : TestingSetup {
    std::shared_ptr<CommitControl> control;
    PoWHashContext pow_context;

    DurabilitySetup() : DurabilitySetup{std::make_shared<CommitControl>()} {}
    explicit DurabilitySetup(std::shared_ptr<CommitControl> commit_control)
        : TestingSetup{ChainType::REGTEST, TestOpts{
              .coins_db_in_memory = false,
              .block_tree_db_in_memory = false,
              .setup_net = false,
              .blockman_opts = [commit_control](kernel::BlockManagerOpts& opts) {
                  opts.prune_target = node::BlockManager::PRUNE_TARGET_MANUAL;
                  opts.file_commit = [commit_control](FILE* file, const fs::path& path) {
                      return commit_control->Commit(file, path);
                  };
              }}}, control{std::move(commit_control)} {}

    CBlock NextBlock()
    {
        node::BlockAssembler::Options opts;
        opts.coinbase_output_script = CScript{} << OP_TRUE;
        opts.include_dummy_extranonce = true;
        CBlock block{node::BlockAssembler{m_node.chainman->ActiveChainstate(), nullptr, opts}.CreateNewBlock()->block};
        node::RegenerateCommitments(block, *m_node.chainman);
        while (!CheckProofOfWork(block, m_node.chainman->GetConsensus(), pow_context)) ++block.nNonce;
        return block;
    }

    CBlock Mine()
    {
        CBlock block{NextBlock()};
        BOOST_REQUIRE(m_node.chainman->ProcessNewBlock(std::make_shared<const CBlock>(block), true, true, nullptr));
        BOOST_REQUIRE_EQUAL(WITH_LOCK(cs_main, return m_node.chainman->ActiveHeight()), WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(block.GetHash())->nHeight));
        return block;
    }

    void Sync()
    {
        BlockValidationState state;
        BOOST_REQUIRE(m_node.chainman->ActiveChainstate().FlushStateToDisk(state, FlushStateMode::FORCE_SYNC));
    }

    void CheckFailure(const std::string& fail_file)
    {
        Mine();
        Sync();
        const auto pending{Mine()};
        auto& chainstate{m_node.chainman->ActiveChainstate()};
        auto& blockman{m_node.chainman->m_blockman};
        auto sub{std::make_shared<FlushSubscriber>()};
        m_node.validation_signals->SyncWithValidationInterfaceQueue();
        m_node.validation_signals->RegisterSharedValidationInterface(sub);
        {
            LOCK(cs_main);
            const auto index_before{Snapshot(*blockman.m_block_tree_db)};
            const auto coins_before{chainstate.CoinsDB().GetBestBlock()};
            const auto dirty_before{chainstate.CoinsTip().GetDirtyCount()};
            BOOST_REQUIRE_GT(dirty_before, 0U);
            control->fail_file = fail_file;
            BlockValidationState state;
            BOOST_CHECK(!chainstate.FlushStateToDisk(state, FlushStateMode::FORCE_FLUSH));
            BOOST_CHECK(state.IsError());
            BOOST_CHECK(!state.IsInvalid());
            BOOST_CHECK_EQUAL(control->failures, 1U);
            BOOST_CHECK_EQUAL(m_node.exit_status.load(), EXIT_FAILURE);
            BOOST_CHECK(bool(*m_node.shutdown_signal));
            BOOST_CHECK(Snapshot(*blockman.m_block_tree_db) == index_before);
            BOOST_CHECK(chainstate.CoinsDB().GetBestBlock() == coins_before);
            BOOST_CHECK_EQUAL(chainstate.CoinsTip().GetDirtyCount(), dirty_before);

            // Shutdown flushes twice. Even if the device recovers, this instance
            // must not publish bookkeeping prepared before the fatal I/O failure.
            control->fail_file.clear();
            const auto calls{control->calls};
            for (int i = 0; i < 2; ++i) {
                BlockValidationState retry;
                BOOST_CHECK(!chainstate.FlushStateToDisk(retry, FlushStateMode::FORCE_SYNC));
                BOOST_CHECK(retry.IsError());
            }
            BOOST_CHECK_EQUAL(control->calls, calls);
            BOOST_CHECK(Snapshot(*blockman.m_block_tree_db) == index_before);
            BOOST_CHECK(chainstate.CoinsDB().GetBestBlock() == coins_before);
            BOOST_CHECK_EQUAL(chainstate.CoinsTip().GetDirtyCount(), dirty_before);
        }
        // No callback should advertise failed durability to wallets/indexes.
        m_node.validation_signals->SyncWithValidationInterfaceQueue();
        BOOST_CHECK_EQUAL(sub->count, 0U);
        m_node.validation_signals->UnregisterSharedValidationInterface(sub);

        // Reopen the last safe on-disk state in a fresh manager. The failed
        // block's raw bytes can be revalidated and connected normally.
        m_node.chainman.reset();
        BOOST_REQUIRE(m_node.shutdown_signal->reset());
        m_node.exit_status = EXIT_SUCCESS;
        m_make_chainman();
        LoadVerifyActivateChainstate();
        BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return m_node.chainman->ActiveHeight()), 1);
        BOOST_REQUIRE(m_node.chainman->ProcessNewBlock(std::make_shared<const CBlock>(pending), true, true, nullptr));
        Sync();
        BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->ActiveChainstate().CoinsDB().GetBestBlock()) == pending.GetHash());
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(blockstorage_durability_tests, DurabilitySetup)

BOOST_AUTO_TEST_CASE(block_sync_failure)
{
    CheckFailure("blk00000.dat");
}

BOOST_AUTO_TEST_CASE(undo_sync_failure)
{
    CheckFailure("rev00000.dat");
}

BOOST_AUTO_TEST_CASE(normal_persistence_and_restart)
{
    const auto block{Mine()};
    Sync();
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChainstate().CoinsDB().GetBestBlock() == block.GetHash());
        CDiskBlockIndex disk_index;
        BOOST_REQUIRE(m_node.chainman->m_blockman.m_block_tree_db->Read(std::make_pair(uint8_t{'b'}, block.GetHash()), disk_index));
        BOOST_CHECK(disk_index.nStatus & BLOCK_HAVE_UNDO);
    }
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    m_node.chainman.reset();
    m_make_chainman();
    LoadVerifyActivateChainstate();
    BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->ActiveTip()->GetBlockHash()) == block.GetHash());
    Sync();
    Mine();
    Sync();
}

BOOST_AUTO_TEST_CASE(large_undo_position_persistence_and_disconnect)
{
    Mine();
    Sync();
    auto& blockman{m_node.chainman->m_blockman};
    {
        // Mark the existing disposable rev file sparse before extending it.
        FILE* raw{fsbridge::fopen(m_args.GetBlocksDirPath() / "rev00000.dat", "rb+")};
        AutoFile file{raw};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_REQUIRE(MakeTestFileSparse(raw));
    }
    constexpr uint64_t boundary{uint64_t{1} << 32};
    {
        LOCK(cs_main);
        blockman.GetBlockFileInfo(0)->nUndoSize = boundary - 4;
    }
    const auto block{Mine()};
    const auto hash{block.GetHash()};
    {
        LOCK(cs_main);
        const auto* index{m_node.chainman->ActiveTip()};
        BOOST_REQUIRE_GT(index->nUndoPos, boundary);
        BOOST_CHECK_GT(blockman.GetBlockFileInfo(0)->nUndoSize, boundary);
        CBlockUndo undo;
        BOOST_REQUIRE(blockman.ReadBlockUndo(undo, *index));
        BOOST_CHECK(undo.vtxundo.empty());
    }
    Sync();
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    m_node.chainman.reset();
    m_make_chainman();
    LoadVerifyActivateChainstate();
    CBlockIndex* index{WITH_LOCK(cs_main, return m_node.chainman->ActiveTip())};
    {
        LOCK(cs_main);
        BOOST_CHECK(index->GetBlockHash() == hash);
        BOOST_REQUIRE_GT(index->nUndoPos, boundary);
        CBlockUndo undo;
        BOOST_REQUIRE(m_node.chainman->m_blockman.ReadBlockUndo(undo, *index));
    }
    BlockValidationState state;
    BOOST_REQUIRE(m_node.chainman->ActiveChainstate().InvalidateBlock(state, index));
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return m_node.chainman->ActiveHeight()), 1);
    {
        LOCK(cs_main);
        m_node.chainman->ActiveChainstate().ResetBlockFailureFlags(index);
        m_node.chainman->RecalculateBestHeader();
    }
    BlockValidationState recovery;
    BOOST_REQUIRE(m_node.chainman->ActiveChainstate().ActivateBestChain(recovery));
    BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->ActiveTip()->GetBlockHash()) == hash);
    Sync();
#ifndef WIN32
    struct stat physical{};
    BOOST_REQUIRE_EQUAL(stat((m_args.GetBlocksDirPath() / "rev00000.dat").c_str(), &physical), 0);
    BOOST_CHECK_LT(static_cast<uint64_t>(physical.st_blocks) * 512, uint64_t{4} << 20);
    BOOST_TEST_MESSAGE("Large undo logical bytes: " << physical.st_size << "; allocated bytes: " << physical.st_blocks * 512);
#endif
}

BOOST_AUTO_TEST_CASE(undo_position_overflow_rejected_before_accounting)
{
    Mine();
    Sync();
    const auto block{NextBlock()};
    const auto hash{block.GetHash()};
    auto& blockman{m_node.chainman->m_blockman};
    LOCK(cs_main);
    CBlockIndex index{block};
    index.phashBlock = &hash;
    index.pprev = m_node.chainman->ActiveTip();
    index.nHeight = 2;
    index.nFile = 0;
    auto* info{blockman.GetBlockFileInfo(0)};
    const auto previous{info->nUndoSize};
    info->nUndoSize = std::numeric_limits<uint64_t>::max() - 1;
    BlockValidationState state;
    BOOST_CHECK(!blockman.WriteBlockUndo(CBlockUndo{}, state, index));
    BOOST_CHECK(state.IsError());
    BOOST_CHECK(!state.IsInvalid());
    BOOST_CHECK_EQUAL(info->nUndoSize, std::numeric_limits<uint64_t>::max() - 1);
    BOOST_CHECK(!(index.nStatus & BLOCK_HAVE_UNDO));
    info->nUndoSize = previous;
    BlockValidationState retry;
    BOOST_CHECK(!blockman.WriteBlockUndo(CBlockUndo{}, retry, index));
    BOOST_CHECK(retry.IsError());
}

BOOST_AUTO_TEST_CASE(rollover_sync_failure)
{
    Mine();
    Sync();
    auto& blockman{m_node.chainman->m_blockman};
    const auto next{NextBlock()};
    LOCK(cs_main);
    // Force a normal file rollover without allocating 128 MiB of test data.
    blockman.GetBlockFileInfo(0)->nSize = node::MAX_BLOCKFILE_SIZE;
    control->fail_file = "blk00000.dat";
    BOOST_CHECK(blockman.WriteBlock(next, 2).IsNull());
    BOOST_CHECK(!fs::exists(blockman.GetBlockPosFilename(FlatFilePos{1, 0})));
    control->fail_file.clear();
    BOOST_CHECK(blockman.WriteBlock(next, 2).IsNull());
    BlockValidationState state;
    BOOST_CHECK(!m_node.chainman->ActiveChainstate().FlushStateToDisk(state, FlushStateMode::FORCE_SYNC));
    BOOST_CHECK(state.IsError());
}

BOOST_AUTO_TEST_CASE(historical_undo_sync_failure)
{
    Mine();
    Sync();
    auto& blockman{m_node.chainman->m_blockman};
    const auto block{NextBlock()};
    LOCK(cs_main);
    // Model an out-of-order download: an older block file still needs undo
    // data after incoming blocks have advanced the write cursor to file 1.
    blockman.GetBlockFileInfo(0)->nSize = node::MAX_BLOCKFILE_SIZE;
    blockman.GetBlockFileInfo(0)->nHeightLast = 2;
    BOOST_REQUIRE(!blockman.WriteBlock(block, 3).IsNull());
    const auto hash{block.GetHash()};
    CBlockIndex index{block};
    index.phashBlock = &hash;
    index.pprev = m_node.chainman->ActiveTip();
    index.nHeight = 2;
    index.nFile = 0;
    control->fail_file = "rev00000.dat";
    BlockValidationState state;
    BOOST_CHECK(!blockman.WriteBlockUndo(CBlockUndo{}, state, index));
    BOOST_CHECK(state.IsError());
    BOOST_CHECK(!(index.nStatus & BLOCK_HAVE_UNDO));
    BOOST_CHECK_EQUAL(control->failures, 1U);
    control->fail_file.clear();
    BlockValidationState retry;
    BOOST_CHECK(!m_node.chainman->ActiveChainstate().FlushStateToDisk(retry, FlushStateMode::FORCE_SYNC));
    BOOST_CHECK(retry.IsError());
}

BOOST_AUTO_TEST_CASE(pruning_sync_failure)
{
    const auto persisted{Mine()};
    Sync();
    auto& blockman{m_node.chainman->m_blockman};
    auto& chainstate{m_node.chainman->ActiveChainstate()};
    const auto next{NextBlock()};
    LOCK(cs_main);
    blockman.GetBlockFileInfo(0)->nSize = node::MAX_BLOCKFILE_SIZE;
    BOOST_REQUIRE(!blockman.WriteBlock(next, 2).IsNull());
    Sync();
    const auto index_before{Snapshot(*blockman.m_block_tree_db)};
    const auto coins_before{chainstate.CoinsDB().GetBestBlock()};
    const fs::path blk0{blockman.GetBlockPosFilename(FlatFilePos{0, 0})};
    const fs::path rev0{blk0.parent_path() / "rev00000.dat"};
    BOOST_REQUIRE(fs::exists(blk0));
    BOOST_REQUIRE(fs::exists(rev0));

    // Only the height is relevant to prune selection. Extend the in-memory
    // chain with test indices to exercise the real MIN_BLOCKS_TO_KEEP cutoff,
    // without spending minutes mining blocks that this error path never reads.
    struct HeightExtension {
        CChain& chain;
        CBlockIndex* original;
        std::vector<std::unique_ptr<CBlockIndex>> indices;
        explicit HeightExtension(CChain& target) : chain{target}, original{target.Tip()}
        {
            auto* prev{original};
            for (unsigned i = 0; i < MIN_BLOCKS_TO_KEEP + 2; ++i) {
                auto index{std::make_unique<CBlockIndex>()};
                index->pprev = prev;
                index->nHeight = prev->nHeight + 1;
                index->phashBlock = original->phashBlock;
                prev = index.get();
                indices.push_back(std::move(index));
            }
            chain.SetTip(*prev);
        }
        ~HeightExtension() { chain.SetTip(*original); }
    } extended_height{chainstate.m_chain};

    control->fail_file = "blk00001.dat";
    BlockValidationState state;
    BOOST_CHECK(!chainstate.FlushStateToDisk(state, FlushStateMode::FORCE_FLUSH, 1));
    BOOST_CHECK(state.IsError());
    BOOST_CHECK_EQUAL(control->failures, 1U);
    BOOST_CHECK(Snapshot(*blockman.m_block_tree_db) == index_before);
    BOOST_CHECK(chainstate.CoinsDB().GetBestBlock() == coins_before);
    BOOST_CHECK(fs::exists(blk0));
    BOOST_CHECK(fs::exists(rev0));
    bool have_pruned{false};
    blockman.m_block_tree_db->ReadFlag("prunedblockfiles", have_pruned);
    BOOST_CHECK(!have_pruned);
    // The memory flag must still reflect the already-prepared prune metadata.
    // Index checks and IsBlockPruned() rely on it during fatal shutdown.
    BOOST_CHECK(blockman.IsBlockPruned(*extended_height.original));
    control->fail_file.clear();
    BlockValidationState retry;
    BOOST_CHECK(!chainstate.FlushStateToDisk(retry, FlushStateMode::FORCE_SYNC));
    BOOST_CHECK(Snapshot(*blockman.m_block_tree_db) == index_before);
    BOOST_CHECK(fs::exists(blk0));
    BOOST_CHECK(fs::exists(rev0));
    blockman.ScanAndUnlinkAlreadyPrunedFiles();
    BOOST_CHECK(fs::exists(blk0));
    BOOST_CHECK(fs::exists(rev0));
    BOOST_CHECK(coins_before == persisted.GetHash());
}

BOOST_AUTO_TEST_CASE(snapshot_completion_stops_after_sync_failure)
{
    Mine();
    auto& chainman{*m_node.chainman};
    auto& validated{chainman.ActiveChainstate()};
    LOCK(cs_main);
    const auto index_before{Snapshot(*chainman.m_blockman.m_block_tree_db)};
    const auto coins_before{validated.CoinsDB().GetBestBlock()};
    auto* target{chainman.ActiveTip()};
    validated.SetTargetBlock(target);
    Chainstate snapshot{nullptr, chainman.m_blockman, chainman, target->GetBlockHash()};
    snapshot.InitCoinsDB(1 << 20, /*in_memory=*/false, /*should_wipe=*/true);
    const auto snapshot_path{snapshot.StoragePath()};
    BOOST_REQUIRE(fs::exists(snapshot_path));

    // Height 1 deliberately has no assumeutxo entry. A failed flush must stop
    // completion before even that later validation/invalidating decision.
    control->fail_file = "blk00000.dat";
    const auto result{chainman.MaybeValidateSnapshot(validated, snapshot)};
    BOOST_CHECK(result == SnapshotCompletionResult::STATS_FAILED);
    BOOST_CHECK_EQUAL(control->failures, 1U);
    BOOST_CHECK(snapshot.m_assumeutxo == Assumeutxo::UNVALIDATED);
    BOOST_CHECK(validated.m_target_blockhash == target->GetBlockHash());
    BOOST_CHECK(fs::exists(snapshot_path));
    BOOST_CHECK(!fs::exists(snapshot_path + "_INVALID"));
    BOOST_CHECK(Snapshot(*chainman.m_blockman.m_block_tree_db) == index_before);
    BOOST_CHECK(validated.CoinsDB().GetBestBlock() == coins_before);
    BOOST_CHECK_EQUAL(m_node.exit_status.load(), EXIT_FAILURE);
    BOOST_CHECK(bool(*m_node.shutdown_signal));
}

BOOST_AUTO_TEST_SUITE_END()

// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <addrman.h>
#include <chainparams.h>
#include <common/args.h>
#include <net_pq.h>
#include <scheduler.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <boost/test/unit_test.hpp>

namespace {
using namespace pq;
constexpr std::array CHAINS{ChainType::MAIN, ChainType::TESTNET, ChainType::SIGNET, ChainType::REGTEST, ChainType::TESTNET4};
struct Fixture : BasicTestingSetup { Fixture() : BasicTestingSetup(ChainType::REGTEST) {} };
Random Coins()
{
    // Public synthetic deterministic draws; never used by a running node.
    return [draw = uint8_t{0}](std::span<uint8_t> out) mutable { std::fill(out.begin(), out.end(), ++draw); return true; };
}
struct SyntheticParams : CChainParams {
    explicit SyntheticParams(const CChainParams& original) : CChainParams(original) {}
    void ChangeChain() { m_chain_type = ChainType::MAIN; }
    void ChangeMagic() { pchMessageStart[0] ^= 1; }
    void ChangeGenesis() { consensus.hashGenesisBlock = uint256{1}; }
    void ChangeChallenge() { consensus.signet_challenge.push_back(0x51); }
};
}

BOOST_FIXTURE_TEST_SUITE(pq_policy_tests, Fixture)
BOOST_AUTO_TEST_CASE(compulsory_policy_has_no_weakening_override)
{
    for (const auto chain : CHAINS) {
        const bool compulsory = chain == ChainType::MAIN || chain == ChainType::TESTNET;
        BOOST_CHECK_EQUAL(RequiredMode(chain), compulsory);
        for (const auto bad : {"", "on", "optional", "prefer", "Required", "required,off"}) {
            BOOST_CHECK_THROW(RequiredMode(chain, bad), std::runtime_error);
        }
        if (compulsory) {
            BOOST_CHECK(RequiredMode(chain, "required"));
            BOOST_CHECK_THROW(RequiredMode(chain, "off"), std::runtime_error);
        } else {
            BOOST_CHECK(!RequiredMode(chain, "off"));
            if (chain == ChainType::REGTEST) BOOST_CHECK(RequiredMode(chain, "required"));
            else BOOST_CHECK_THROW(RequiredMode(chain, "required"), std::runtime_error);
        }
    }
    BOOST_CHECK_THROW(RequiredMode(static_cast<ChainType>(255)), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(exact_chain_discriminators_and_frozen_regtest_preservation)
{
    ArgsManager args;
    const auto reg = CreateChainParams(args, ChainType::REGTEST);
    const auto test = CreateChainParams(args, ChainType::TESTNET);
    BOOST_CHECK_EQUAL(HexStr(NetworkForChain(*reg)), "03fcc1b7dc85f628700e75cd2c03b19de99605ea1bd80ecc294344696e126ba1b3ef08238e0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004");
    BOOST_CHECK_EQUAL(HexStr(NetworkForChain(*test)), "01d7dbeea42069ffbedcb99d0701f69ab941fdb6d22ab28472836acfd145dc899a7c70fcb30c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004");
    for (size_t n = 0; n < CHAINS.size(); ++n) {
        const auto params = CreateChainParams(args, CHAINS[n]);
        const auto net = NetworkForChain(*params);
        BOOST_CHECK_EQUAL(net.size(), 85U);
        BOOST_CHECK_EQUAL(net[0], n);
        BOOST_CHECK_EQUAL(HexStr(std::span{net}.subspan(1,4)), HexStr(params->MessageStart()));
        BOOST_CHECK_EQUAL(HexStr(std::span{net}.subspan(5,32)), HexStr(params->GetConsensus().hashGenesisBlock));
    }
    const auto original = NetworkForChain(*test);
    SyntheticParams chain(*test), magic(*test), genesis(*test);
    chain.ChangeChain(); magic.ChangeMagic(); genesis.ChangeGenesis();
    for (const auto* changed : {&chain, &magic, &genesis}) BOOST_CHECK(NetworkForChain(*changed) != original);
    ArgsManager signet_args;
    signet_args.ForceSetArg("-signetchallenge", "51");
    const auto signet = CreateChainParams(signet_args, ChainType::SIGNET);
    SyntheticParams challenge(*signet); challenge.ChangeChallenge();
    const auto first = NetworkForChain(*signet), second = NetworkForChain(challenge);
    // Same chain/magic/genesis isolates the challenge-hash discriminator itself.
    BOOST_CHECK(std::equal(first.begin(), first.begin()+37, second.begin()));
    BOOST_CHECK(first != second);
    BOOST_CHECK_EQUAL(HexStr(std::span{first}.last(48)), "c66a74deabfeab86cdfd9fe16d8e20915087ae3b3627113bac0bd739822c07a609b45b29bca82501a6dc4aefe09def41");
}

BOOST_AUTO_TEST_CASE(all_chain_pairs_authenticate_only_the_same_network)
{
    ArgsManager args;
    auto identity = std::make_shared<Identity>();
    const std::array<uint8_t,32> seed{17}; BOOST_REQUIRE(identity->FromSeed(seed));
    for (const auto a : CHAINS) for (const auto b : CHAINS) {
        const auto pa = CreateChainParams(args,a), pb = CreateChainParams(args,b);
        Handshake i(true,NetworkForChain(*pa),{},identity,Coins()), r(false,NetworkForChain(*pb),{},identity,Coins());
        Bytes wire, next; BOOST_REQUIRE(i.Start(wire)); wire.erase(wire.begin(),wire.begin()+16);
        if (a != b) {
            BOOST_CHECK(!r.Advance(wire,next)); BOOST_CHECK(next.empty());
            BOOST_CHECK(!r.Ready()); BOOST_CHECK(!i.Ready());
            continue;
        }
        for (unsigned step=1;step<=5;++step) {
            BOOST_REQUIRE((step % 2 ? r : i).Advance(wire,next)); wire=std::move(next);
            if (step==1) wire.erase(wire.begin(),wire.begin()+16);
        }
        BOOST_CHECK(i.Ready() && r.Ready());
        BOOST_CHECK(i.TakeKeys().id == r.TakeKeys().id);
    }
}

BOOST_AUTO_TEST_CASE(every_node_connection_type_overrides_classical_preferences)
{
    constexpr std::array TYPES{ConnectionType::INBOUND,ConnectionType::OUTBOUND_FULL_RELAY,ConnectionType::BLOCK_RELAY,
        ConnectionType::MANUAL,ConnectionType::ADDR_FETCH,ConnectionType::FEELER,ConnectionType::PRIVATE_BROADCAST};
    NodeId id{0};
    for (const auto type : TYPES) for (bool v2 : {false,true}) for (const auto services :
        {NODE_NONE,NODE_P2P_V2,NODE_P2P_PQ,ServiceFlags(NODE_P2P_V2|NODE_P2P_PQ)}) {
        auto context = std::make_shared<Context>(SelectedNetwork());
        CAddress address; address.nServices=services;
        CNode node(++id,{},address,0,0,CService{},"",type,false,0,
            CNodeOptions{.use_v2transport=v2,.pq_context=context});
        BOOST_CHECK(node.m_transport->GetInfo().transport_type == TransportProtocolType::PQ_V1);
        BOOST_CHECK(!node.m_transport->ShouldReconnectV1());
        BOOST_CHECK(!node.m_transport->GetInfo().pq_authenticated);
        node.m_transport->Disconnect();
        BOOST_CHECK(!node.m_transport->GetInfo().session_id);
        BOOST_CHECK(!node.m_transport->Poll());
    }
}
BOOST_AUTO_TEST_CASE(connection_manager_rejects_policy_bypass_before_binding)
{
    ArgsManager args;
    NetGroupManager groups{NetGroupManager::NoAsmap()};
    AddrMan addresses{groups,true,0};
    CScheduler scheduler;
    for (const auto chain : {ChainType::MAIN,ChainType::TESTNET}) {
        const auto params=CreateChainParams(args,chain);
        CConnman connman{0,0,addresses,groups,*params,false};
        CConnman::Options options;
        BOOST_CHECK(!connman.Start(scheduler,options)); // Missing compulsory context.
        options.pq_context=std::make_shared<Context>(SelectedNetwork());
        BOOST_CHECK(!connman.Start(scheduler,options)); // Wrong network, no bind.
    }
    BOOST_CHECK(serviceFlagsToStr(NODE_P2P_PQ) == std::vector<std::string>{"P2P_PQ_EXPERIMENTAL"});
}
BOOST_AUTO_TEST_SUITE_END()

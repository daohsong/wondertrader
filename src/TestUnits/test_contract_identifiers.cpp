#include "gtest/gtest.h"

#include <cstdio>
#include <fstream>
#include <string>

#include "../Includes/WTSContractInfo.hpp"
#include "../WTSTools/WTSBaseDataMgr.h"

USING_NS_WTP;

namespace
{
std::string writeContractConfig(const char* name, const std::string& content)
{
	const std::string path = std::string("/tmp/") + name;
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << content;
	return path;
}
}

TEST(ContractIdentifiers, BuildsExactCommodityAndContractIdentifiers)
{
	WTSCommodityInfo* commodity = WTSCommodityInfo::create("IF", "index future", "CFFEX", "TRADING", "HOLIDAY");
	ASSERT_NE(commodity, nullptr);
	EXPECT_STREQ(commodity->getFullPid(), "CFFEX.IF");
	commodity->release();

	WTSContractInfo* contract = WTSContractInfo::create("IF2606", "index future", "CFFEX", "IF");
	ASSERT_NE(contract, nullptr);
	EXPECT_STREQ(contract->getFullCode(), "CFFEX.IF2606");
	EXPECT_STREQ(contract->getFullPid(), "CFFEX.IF");
	ASSERT_TRUE(contract->setAltCode("IF606"));
	EXPECT_STREQ(contract->getFullAltCode(), "CFFEX.IF606");
	contract->release();
}

TEST(ContractIdentifiers, AcceptsIdentifiersAtTheFixedBufferBoundary)
{
	const std::string exchange(31, 'E');
	const std::string product(31, 'P');
	const std::string code(31, 'C');
	const std::string name(63, 'N');

	WTSCommodityInfo* commodity = WTSCommodityInfo::create(
		product.c_str(), name.c_str(), exchange.c_str(), name.c_str(), name.c_str(), name.c_str());
	ASSERT_NE(commodity, nullptr);
	EXPECT_EQ(std::string(commodity->getFullPid()), exchange + "." + product);
	commodity->release();

	WTSContractInfo* contract = WTSContractInfo::create(code.c_str(), name.c_str(), exchange.c_str(), product.c_str());
	ASSERT_NE(contract, nullptr);
	EXPECT_EQ(std::string(contract->getFullCode()), exchange + "." + code);
	EXPECT_EQ(std::string(contract->getFullPid()), exchange + "." + product);
	ASSERT_TRUE(contract->setAltCode(code.c_str()));
	EXPECT_EQ(std::string(contract->getFullAltCode()), exchange + "." + code);
	contract->release();
}

TEST(ContractIdentifiers, RejectsInputsThatCannotBeRepresentedWithoutTruncation)
{
	const std::string tooLongField(64, 'X');
	const std::string tooLongContractField(32, 'Y');

	EXPECT_EQ(WTSCommodityInfo::create(
		"IF", tooLongField.c_str(), "CFFEX", "TRADING", "HOLIDAY"), nullptr);
	EXPECT_EQ(WTSCommodityInfo::create(
		tooLongField.c_str(), "name", "CFFEX", "TRADING", "HOLIDAY"), nullptr);
	EXPECT_EQ(WTSContractInfo::create(
		tooLongContractField.c_str(), "name", "CFFEX", "IF"), nullptr);
	EXPECT_EQ(WTSContractInfo::create(
		"IF2606", "name", tooLongContractField.c_str(), "IF"), nullptr);

	WTSContractInfo* contract = WTSContractInfo::create("IF2606", "name", "CFFEX", "IF");
	ASSERT_NE(contract, nullptr);
	ASSERT_TRUE(contract->setAltCode("IF606"));
	EXPECT_FALSE(contract->setAltCode(tooLongContractField.c_str()));
	EXPECT_STREQ(contract->getAltCode(), "IF606");
	EXPECT_STREQ(contract->getFullAltCode(), "CFFEX.IF606");
	contract->release();
}

TEST(ContractIdentifiers, RejectsNullInputs)
{
	EXPECT_EQ(WTSCommodityInfo::create(nullptr, "name", "CFFEX", "TRADING", "HOLIDAY"), nullptr);
	EXPECT_EQ(WTSContractInfo::create(nullptr, "name", "CFFEX", "IF"), nullptr);
}

TEST(ContractIdentifiers, BaseDataLoaderSkipsUnrepresentableContracts)
{
	const std::string sessions = writeContractConfig("wt_contract_id_sessions.json", R"({
		"TRADING": {"name":"trading", "offset":0, "sections":[{"from":900,"to":1500}]}
	})");
	const std::string longProduct(64, 'P');
	const std::string commodities = writeContractConfig("wt_contract_id_commodities.json",
		std::string("{\"CFFEX\":{")
		+ "\"IF\":{\"name\":\"index future\",\"session\":\"TRADING\",\"holiday\":\"\"},"
		+ "\"" + longProduct + "\":{\"name\":\"too long\",\"session\":\"TRADING\",\"holiday\":\"\"}}}");
	const std::string longCode(32, 'C');
	const std::string longAltCode(32, 'A');
	const std::string contracts = writeContractConfig("wt_contract_id_contracts.json",
		std::string("{\"CFFEX\":{")
		+ "\"" + longCode + "\":{\"name\":\"too long\",\"exchg\":\"CFFEX\",\"product\":\"IF\"},"
		+ "\"IF2606\":{\"name\":\"bad alt\",\"exchg\":\"CFFEX\",\"product\":\"IF\",\"altcode\":\""
		+ longAltCode + "\"}}}");

	WTSBaseDataMgr manager;
	ASSERT_TRUE(manager.loadSessions(sessions.c_str()));
	ASSERT_TRUE(manager.loadCommodities(commodities.c_str()));
	ASSERT_NE(manager.getCommodity("CFFEX.IF"), nullptr);
	EXPECT_EQ(manager.getCommodity((std::string("CFFEX.") + longProduct).c_str()), nullptr);
	ASSERT_TRUE(manager.loadContracts(contracts.c_str()));
	EXPECT_EQ(manager.getContractSize(), 0U);

	std::remove(sessions.c_str());
	std::remove(commodities.c_str());
	std::remove(contracts.c_str());
}

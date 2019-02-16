// Copyright (c) 2009-2014 The WaykiChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "rpcserver.h"
#include "init.h"
#include "main.h"
#include "sync.h"
#include "../wallet/wallet.h"

#include <fstream>
#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "json/json_spirit_value.h"
#include "json/json_spirit_writer_template.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_reader.h"
#include "json/json_spirit_stream_reader.h"


using namespace json_spirit;
using namespace std;

void EnsureWalletIsUnlocked();

string static EncodeDumpTime(int64_t nTime) {
    return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

//int64_t static DecodeDumpTime(const string &str) {
//    static const boost::posix_time::ptime epoch = boost::posix_time::from_time_t(0);
//    static const locale loc(locale::classic(),
//        new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%SZ"));
//    istringstream iss(str);
//    iss.imbue(loc);
//    boost::posix_time::ptime ptime(boost::date_time::not_a_date_time);
//    iss >> ptime;
//    if (ptime.is_not_a_date_time())
//        return 0;
//    return (ptime - epoch).total_seconds();
//}

//string static EncodeDumpString(const string &str) {
//    stringstream ret;
//    for (auto c : str) {
//        if (c <= 32 || c >= 128 || c == '%') {
//            ret << '%' << HexStr(&c, &c + 1);
//        } else {
//            ret << c;
//        }
//    }
//    return ret.str();
//}

string DecodeDumpString(const string &str) 
{
    stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++) {
        unsigned char c = str[pos];
        if (c == '%' && pos+2 < str.length()) {
            c = (((str[pos+1]>>6)*9+((str[pos+1]-'0')&15)) << 4) | 
                ((str[pos+2]>>6)*9+((str[pos+2]-'0')&15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

Value dropminerkeys(const Array& params, bool fHelp)
{
	if (fHelp || params.size() != 0) {
		throw runtime_error("dropminerkeys \n"
			    "\ndrop all miner keys in a wallet for cool mining.\n"
			    "\nResult:\n"
			    "\nExamples:\n"
			    + HelpExampleCli("dropminerkeys", "")
			    + HelpExampleRpc("dropminerkeys", "")
		);
    }

	EnsureWalletIsUnlocked();
	if (!pwalletMain->IsReadyForCoolMiner(*pAccountViewTip)) {
		throw runtime_error("there is no cool miner key or miner key is not registered yet");
	}

	pwalletMain->ClearAllCkeyForCoolMiner();
	Object ret;
	ret.push_back( Pair("info", "wallet is ready for cool miner to mine.") );
	return ret;
}

Value importprivkey(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "importprivkey \"WICC privkey\" ( \"label\" rescan )\n"
            "\nAdds a private key (as returned by dumpprivkey) to your wallet.\n"
            "\nArguments:\n"
            "1. \"WICC privkey\"   (string, required) The private key (see dumpprivkey)\n"
            "2. \"label\"            (string, optional) an optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
            "\nExamples:\n"
            "\nDump a private key\n"
            + HelpExampleCli("dumpprivkey", "\"myaddress\"") +
            "\nImport the private key\n"
            + HelpExampleCli("importprivkey", "\"mykey\"") +
            "\nImport using a label\n"
            + HelpExampleCli("importprivkey", "\"mykey\" \"testing\" false") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("importprivkey", "\"mykey\", \"testing\", false")
        );

    EnsureWalletIsUnlocked();

    string strSecret = params[0].get_str();
//  string strLabel = "";
//  if (params.size() > 1)
//      strLabel = params[1].get_str();
//
//	bool fRescan(true);    // Whether to perform rescan after import
//	if (params.size() > 2) {
//		fRescan = params[2].get_bool();
//	}

    CCoinSecret vchSecret;
    bool fGood = vchSecret.SetString(strSecret);

    if (!fGood) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

    CKey key = vchSecret.GetKey();
    if (!key.IsValid()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

    CPubKey pubkey = key.GetPubKey();
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

       if (!pwalletMain->AddKey(key))
           throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

    }
    Object reply2;
    reply2.push_back(Pair("imported_key_address",pubkey.GetKeyID().ToAddress()));
    return reply2;
}

Value importwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "importwallet \"filename\"\n"
            "\nImports keys from a wallet dump file (see dumpwallet).\n"
            "\nArguments:\n"
            "1. \"filename\"    (string, required) The wallet file to be imported\n"
            "\nExamples:\n"
            "\nDump the wallet first\n"
            + HelpExampleCli("dumpwallet", "\"target_dumpwallet_filepath\"") +
            "\nImport the wallet\n"
            + HelpExampleCli("importwallet", "\"target_dumpwallet_filepath\"") +
            "\nImport using the json rpc call\n"
            + HelpExampleRpc("importwallet", "\"target_dumpwallet_filepath\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    ifstream file;
    file.open(params[0].get_str().c_str(), ios::in | ios::ate);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

//  int64_t nTimeBegin = chainActive.Tip()->nTime;

//  bool fGood = true;
//  int64_t nFilesize = max((int64_t)1, (int64_t)file.tellg());
    file.seekg(0, file.beg);
    int importedKeySize = 0;
    pwalletMain->ShowProgress(_("Importing..."), 0); // show progress dialog in GUI
    if (file.good()) {
    	Value reply;
    	json_spirit::read(file, reply);
    	const Value & keyobj = find_value(reply.get_obj(),"key");
    	const Array & keyarry = keyobj.get_array();
    	for (auto const &keyItem :keyarry) {
    		CKeyCombi keyCombi;
    		const Value &obj = find_value(keyItem.get_obj(), "keyid");
    		if(obj.type() == null_type)
    			continue;
    		string strKeyId = find_value(keyItem.get_obj(), "keyid").get_str();
    		CKeyID keyId(uint160(ParseHex(strKeyId)));
    		keyCombi.UnSerializeFromJson(keyItem.get_obj());
    		if(!keyCombi.IsContainMainKey() && !keyCombi.IsContainMinerKey()) {
    			continue;
    		}
    		if(pwalletMain->AddKey(keyId, keyCombi))
    			importedKeySize++;
    	}
    }
    file.close();
    pwalletMain->ShowProgress("", 100); // hide progress dialog in GUI
    pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);

    Object reply2;
    reply2.push_back(Pair("imported_key_size", importedKeySize));
    return reply2;
}

Value dumpprivkey(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "dumpprivkey \"WICC address\"\n"
            "\nReveals the private key corresponding to 'WICC address'.\n"
            "Then the importprivkey can be used with this output\n"
            "\nArguments:\n"
            "1. \"Coinaddress\"   (string, required) The Coin address for the private key\n"
            "\nResult:\n"
            "\"key\"                (string) The private key\n"
            "\nExamples:\n"
            + HelpExampleCli("dumpprivkey", "\"myaddress\"")
            + HelpExampleCli("importprivkey", "\"mykey\"")
            + HelpExampleRpc("dumpprivkey", "\"myaddress\"")
        );

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();
    CCoinAddress address;
    if (!address.SetString(strAddress))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Coin address");
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CKey vchSecret;
    if (!pwalletMain->GetKey(keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    CKey minerkey;
#if 0 //modified by shane @2018/5/17 此处不能抛异常
    if (!pwalletMain->GetKey(keyID, minerkey,true))
           throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
#else
	pwalletMain->GetKey(keyID, minerkey,true);
#endif
    Object reply;
    	reply.push_back(Pair("privkey", CCoinSecret(vchSecret).ToString()));
    if(minerkey.IsValid() && minerkey.ToString() != vchSecret.ToString())
    	reply.push_back(Pair("minerkey", CCoinSecret(minerkey).ToString()));
    else
    	reply.push_back(Pair("minerkey", " "));
    return reply;
}

Value dumpwallet(const Array& params, bool fHelp) {
	if (fHelp || params.size() != 1)
		throw runtime_error("dumpwallet \"filename\"\n"
				"\nDumps all wallet keys in a human-readable format.\n"
				"\nArguments:\n"
				"1. \"filename\"    (string, required) The filename\n"
				"\nExamples:\n" + HelpExampleCli("dumpwallet", "\"test\"") + HelpExampleRpc("dumpwallet", "\"test\""));

	EnsureWalletIsUnlocked();

	ofstream file;
	file.open(params[0].get_str().c_str());
	if (!file.is_open())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

	Object reply;
	reply.push_back(Pair("created by Coin", CLIENT_BUILD + CLIENT_DATE));
	reply.push_back(Pair("Created Time ", EncodeDumpTime(GetTime())));
	reply.push_back(Pair("Best block index hight ", chainActive.Height()));
	reply.push_back(Pair("Best block hash ", chainActive.Tip()->GetBlockHash().ToString()));

	set<CKeyID> setKeyIds;
	pwalletMain->GetKeys(setKeyIds);
	Array arrKeys;
	for (auto & keyId : setKeyIds) {
		CKeyCombi keyCombi;
		pwalletMain->GetKeyCombi(keyId, keyCombi);
		Object obj = keyCombi.ToJsonObj();
		obj.push_back(Pair("keyid", keyId.ToString()));
		arrKeys.push_back(obj);
	}
	reply.push_back(Pair("key", arrKeys));
	file <<  write_string(Value(reply), true);
	file.close();
	Object reply2;
	reply2.push_back(Pair("info", "dump ok"));
	reply2.push_back(Pair("key size", (int)setKeyIds.size()));
	return reply2;
}